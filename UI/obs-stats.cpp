#include <util/curl/curl-helper.h>
#include <string>
#include "util/platform.h"
#include "../UI/obs-frontend-api/obs-frontend-api.h"
#include <random>
#include <sstream>
#include <chrono>
#include <QSysInfo>
#include <QString>
#include <float.h>
#include <obs.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>

curl_slist* headers = NULL;
CURL* curl;

static std::random_device		crypto_device;
static std::mt19937			generator(crypto_device());
static std::uniform_int_distribution<>	uni_dist(0, 15);
static std::uniform_int_distribution<>	uni_dist2(8, 11);

os_cpu_usage_info_t*			cpu_usage_info;
std::string				generated_uuid;
std::chrono::steady_clock::time_point	timer(std::chrono::high_resolution_clock::now());

std::string				url(LOUPER_STATS_URL);
std::chrono::seconds			delay(3);

uint64_t				lastBytesSent = 0;
uint64_t				lastBytesSentTime = 0;

bool					stream_active = false;
std::thread				updater;
bool					updating = false;
std::mutex				waiter;
std::condition_variable			cond_waiter;

std::mutex				send_lock;

static const char* format_str[] = {
	"VIDEO_FORMAT_NONE",

	/* planar 420 format */
	"VIDEO_FORMAT_I420", /* three-plane */
	"VIDEO_FORMAT_NV12", /* two-plane, luma and packed chroma */

	/* packed 422 formats */
	"VIDEO_FORMAT_YVYU",
	"VIDEO_FORMAT_YUY2", /* YUYV */
	"VIDEO_FORMAT_UYVY",

	/* packed uncompressed formats */
	"VIDEO_FORMAT_RGBA",
	"VIDEO_FORMAT_BGRA",
	"VIDEO_FORMAT_BGRX",
	"VIDEO_FORMAT_Y800", /* grayscale */

	/* planar 4:4:4 */
	"VIDEO_FORMAT_I444",

	/* more packed uncompressed formats */
	"VIDEO_FORMAT_BGR3",

	/* planar 4:2:2 */
	"VIDEO_FORMAT_I422",

	/* planar 4:2:0 with alpha */
	"VIDEO_FORMAT_I40A",

	/* planar 4:2:2 with alpha */
	"VIDEO_FORMAT_I42A",

	/* planar 4:4:4 with alpha */
	"VIDEO_FORMAT_YUVA",

	/* packed 4:4:4 with alpha */
	"VIDEO_FORMAT_AYUV"
};

static const char* colorspace_str[] = {
	"VIDEO_CS_DEFAULT",
	"VIDEO_CS_601",
	"VIDEO_CS_709",
	"VIDEO_CS_SRGB"
};

static const char* range_str[] = {
	"VIDEO_RANGE_DEFAULT",
	"VIDEO_RANGE_PARTIAL",
	"VIDEO_RANGE_FULL"
};

static const char* speaker_str[] = {
	"SPEAKERS_UNKNOWN",     /**< Unknown setting, fallback is stereo. */
	"SPEAKERS_MONO",        /**< Channels: MONO */
	"SPEAKERS_STEREO",      /**< Channels: FL, FR */
	"SPEAKERS_2POINT1",     /**< Channels: FL, FR, LFE */
	"SPEAKERS_4POINT0",     /**< Channels: FL, FR, FC, RC */
	"SPEAKERS_4POINT1",     /**< Channels: FL, FR, FC, LFE, RC */
	"SPEAKERS_5POINT1",     /**< Channels: FL, FR, FC, LFE, RL, RR */
	"SPEAKERS_7POINT1"	/**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
};

std::string generate_uuid() {
	std::stringstream ss;
	int i;
	ss << std::hex;
	for (i = 0; i < 8; i++) {
		ss << uni_dist(generator);
	}
	ss << "-";
	for (i = 0; i < 4; i++) {
		ss << uni_dist(generator);
	}
	ss << "-4";
	for (i = 0; i < 3; i++) {
		ss << uni_dist(generator);
	}
	ss << "-";
	ss << uni_dist2(generator);
	for (i = 0; i < 3; i++) {
		ss << uni_dist(generator);
	}
	ss << "-";
	for (i = 0; i < 12; i++) {
		ss << uni_dist(generator);
	};

	return ss.str();
}

struct permanent_init
{
	permanent_init()
	{
		cpu_usage_info = os_cpu_usage_info_start();
		generated_uuid = generate_uuid();

		curl = curl_easy_init();
		if (!curl)
		{
			//blog(LOG_ERROR, "Cannot init cURL");
			curl_easy_cleanup(curl);
		}
		else
		{
			headers = curl_slist_append(headers, "Accept: application/json");
			headers = curl_slist_append(headers, "Content-Type: application/json");
			headers = curl_slist_append(headers, "charset: utf-8");
			headers = curl_slist_append(headers, "Authorization: Basic " LOUPER_STATS_AUTH_SECRET);

			curl_easy_setopt(curl, CURLOPT_POST, 1);
			curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
		}
	}
} functor_cpu_usage;


static void gen_processor_cores(std::string& info_out)
{
	info_out += "\"physical_cores\": ";
	info_out += std::to_string(os_get_physical_cores()) + ", ";
	info_out += "\"logical_cores\": ";
	info_out += std::to_string(os_get_logical_cores()) + ", ";
}

static void gen_os_info(std::string& info_out)
{
	info_out += "\"os_name\": \"";
	info_out += QSysInfo::productType().toStdString() + "\", ";
	info_out += "\"os_version\": \"";
	info_out += QSysInfo::prettyProductName().toStdString() + "\"";
}

extern void gen_available_memory(std::string& info_out);
extern void gen_processor_info(std::string& info_out);

static void gen_system_specs(std::string& info_out)
{
	info_out += "\"system_specs\": { ";
	gen_processor_info(info_out);
	gen_processor_cores(info_out);
	gen_available_memory(info_out);
	gen_os_info(info_out);
	info_out += " }";
}

static void gen_application_settings(std::string& info_out)
{
	OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();

	OBSDataAutoRelease settings = obs_output_get_settings(stream_output);

	const struct video_output_info* voi = video_output_get_info(obs_get_video());
	obs_video_info vvi;
	bool result = obs_get_video_info(&vvi);
	int base_width = 0;
	int base_height = 0;

	if (result)
	{
		base_width = vvi.base_width;
		base_height = vvi.base_height;
	}

	const struct audio_output_info* aoi = audio_output_get_info(obs_get_audio());

	info_out += "\"application_settings\": { ";

	info_out += "\"video\": { ";
	info_out += "\"base_width\": ";
	info_out += std::to_string(base_width) + ", ";

	info_out += "\"base_height\": ";
	info_out += std::to_string(base_height) + ", ";

	info_out += "\"output_width\": ";
	info_out += std::to_string(obs_output_get_width(stream_output)) + ", ";

	info_out += "\"output_height\": ";
	info_out += std::to_string(obs_output_get_height(stream_output)) + ", ";

	info_out += "\"fps\": ";
	info_out += std::to_string(voi->fps_num / voi->fps_den) + ", ";

	info_out += "\"video_format\": \"";
	info_out += std::string(format_str[voi->format]) + "\", ";

	info_out += "\"video_colorspace\": \"";
	info_out += std::string(colorspace_str[voi->colorspace]) + "\", ";

	info_out += "\"video_range_type\": \"";
	info_out += std::string(range_str[voi->range]) + "\" }, ";


	obs_encoder_t* aencoder = obs_output_get_audio_encoder(stream_output, 0);
	OBSDataAutoRelease asettings = obs_encoder_get_settings(aencoder);

	info_out += "\"audio\": { ";
	info_out += "\"sample_rate\": \"";
	info_out += std::to_string(obs_encoder_get_sample_rate(aencoder)) + " KHz\", ";

	info_out += "\"channels\": \"";
	info_out += std::string(speaker_str[aoi->speakers]) + "\" } }";
}

static void gen_encoder_audio_bitrate(std::string& info_out)
{
	OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();

	obs_encoder_t* aencoder = obs_output_get_audio_encoder(stream_output, 0);
	OBSDataAutoRelease asettings = obs_encoder_get_settings(aencoder);

	info_out += std::to_string(obs_data_get_int(asettings, "bitrate"));
}

static void gen_encoder_video_bitrate(std::string& info_out)
{
	OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();

	obs_encoder_t* vencoder = obs_output_get_video_encoder(stream_output);
	OBSDataAutoRelease settings = obs_encoder_get_settings(vencoder);

	info_out += std::to_string((int)obs_data_get_int(settings, "bitrate"));
}

static void gen_encoder_settings(std::string& info_out)
{
	OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();

	obs_encoder_t* vencoder = obs_output_get_video_encoder(stream_output);
	obs_encoder_t* aencoder = obs_output_get_audio_encoder(stream_output, 0);
	OBSDataAutoRelease settings = obs_encoder_get_settings(vencoder);
	OBSDataAutoRelease asettings = obs_encoder_get_settings(aencoder);

	const struct video_output_info* voi = video_output_get_info(obs_get_video());

	info_out += "\"encoder_settings\": { ";

	info_out += "\"video\": { ";
	info_out += "\"bitrate_target\": ";
	info_out += std::to_string((int)obs_data_get_int(settings, "bitrate")) + ", ";

	info_out += "\"fps\": ";
	info_out += std::to_string(voi->fps_num / voi->fps_den) + ", ";

	info_out += "\"width\": ";
	info_out += std::to_string(obs_encoder_get_width(vencoder)) + ", ";

	info_out += "\"height\": ";
	info_out += std::to_string(obs_encoder_get_height(vencoder)) + ", ";

	info_out += "\"keyint\": ";
	info_out += std::to_string(obs_data_get_int(settings, "keyint_sec")) + " }, ";

	bool audio_cbr = obs_data_get_bool(asettings, "cbr");
	std::string audio_cbr_out = audio_cbr ? "yes" : "no";

	info_out += "\"audio\": { ";
	info_out += "\"mode\": \"";
	info_out += std::string(obs_encoder_get_name(aencoder)) + "\", ";

	info_out += "\"bitrate_target\": ";
	info_out += std::to_string(obs_data_get_int(asettings, "bitrate")) + ", ";

	info_out += "\"sample_rate\": ";
	info_out += std::to_string(obs_encoder_get_sample_rate(aencoder)) + ", ";

	info_out += "\"cbr\": \"";
	info_out += audio_cbr_out + "\", ";

	info_out += "\"output_buffer\": ";
	info_out += std::to_string(obs_data_get_int(asettings, "buffer_size")) + " } }";
}

static void gen_lde_version(std::string& info_out)
{
	info_out += "\"lbs_version\": \"HB-LDE v" + std::string{obs_get_version_string()} + "\"";
}


static void gen_current_scene(std::string& info_out)
{

	auto cb = [](obs_scene_t*, obs_sceneitem_t* scene_item, void* param) {

		auto out_ptr = static_cast<std::string*>(param);
		obs_source_t* item_source = obs_sceneitem_get_source(scene_item);
		const char* id = obs_source_get_id(item_source);
		const char* name = obs_source_get_name(item_source);


		out_ptr->append("{ \"name\": \"");
		out_ptr->append(std::string(name) + "\", ");


		out_ptr->append("\"type\": \"");
		out_ptr->append(std::string(id) + "\" }, ");

		return true;
	};

	const char* current_scene_name;

	{
		OBSSource current_scene = obs_frontend_get_current_scene();
		obs_source_release(current_scene);
		current_scene_name = obs_source_get_name(current_scene);
	}

	OBSSceneAutoRelease scene_ptr = obs_get_scene_by_name(current_scene_name);

	if (current_scene_name)
	{
		info_out += "\"name\": \"";
		info_out += std::string(current_scene_name) + "\", ";
		info_out += "\"sources\": ";

		std::string scene_sources;
		obs_scene_enum_items(scene_ptr, cb, &scene_sources);

		if (scene_sources.length() < 2)
		{
			info_out += "null";
		}
		else
		{
			info_out += "[ ";

			info_out += scene_sources;

			//delete , and space symbols
			info_out.pop_back();
			info_out.pop_back();

			info_out += " ]";
		}
	}
	else
		info_out += "null";

}

static uint64_t get_output_duration(obs_output_t* output)
{
	if (!output || !obs_output_active(output))
		return 0;

	video_t* video = obs_output_video(output);
	uint64_t frameTimeNs = video_output_get_frame_time(video);
	int totalFrames = obs_output_get_total_frames(output);

	return util_mul_div64(totalFrames, frameTimeNs, 1000000ULL);
}

static std::string get_destination_key(obs_service_t* service)
{
    std::string stream_key;

    const char* temp_key = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_STREAM_KEY);
    if (temp_key != NULL)
    {
        stream_key = temp_key;
    }
    else
    {
        stream_key = "null";
    }

    return stream_key;
}

static void gen_destination(std::string& info_out)
{
    OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();
    obs_service_t* service = obs_output_get_service(stream_output);

    std::string stream_url;
    std::string stream_protocol;

    const char* temp_url = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
    if (temp_url != NULL)
    {
        stream_url = temp_url;
    }
    else
    {
        stream_url = "null";
    }

    const char* temp_protocol = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
    if (temp_protocol != NULL)
    {
        stream_protocol = temp_protocol;
    }
    else
    {
        stream_protocol = "null";
    }

    info_out += "\"stream_url\": \"";
    info_out += stream_url + "\", ";

    info_out += "\"stream_key\": \"";
    info_out += get_destination_key(service) + "\", ";

    info_out += "\"protocol\": \"";
    info_out += stream_protocol + "\"";
}

static void gen_status(std::string& info_out, bool is_starting)
{
	std::string status;

	//obs_frontend_streaming_active();
	if (is_starting)
	{
		status = "true";
	}
	else
		status = "false";

	info_out += "\"streaming\": ";
	info_out += status;

}


static void gen_stats(std::string& info_out)
{
	OBSOutputAutoRelease stream_output = obs_frontend_get_streaming_output();

	uint64_t bytesSent = obs_output_get_total_bytes(stream_output);
	uint64_t bytesSentTime = os_gettime_ns();

	if (bytesSent < lastBytesSent)
		bytesSent = 0;
	if (bytesSent == 0)
		lastBytesSent = 0;

	uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;

	double timePassed =
		double(bytesSentTime - lastBytesSentTime) / 1000000000.0;

	double kbitsPerSec = double(bitsBetween) / timePassed / 1000.0;

	int bitrate = kbitsPerSec;

	lastBytesSent = bytesSent;
	lastBytesSentTime = bytesSentTime;

	double cpu_usage = os_cpu_usage_info_query(cpu_usage_info);

	video_t* video = obs_get_video();
	obs_encoder_t* vencoder = obs_output_get_video_encoder(stream_output);
	OBSDataAutoRelease settings = obs_encoder_get_settings(vencoder);

	if (bitrate == 0)
		bitrate = (int)obs_data_get_int(settings, "bitrate"); //means a bitrate was set on settings

	info_out += "\"fps_current\": ";
	info_out += std::to_string(obs_get_active_fps()) + ", ";

	info_out += "\"bitrate_current\": ";
	info_out += std::to_string(bitrate) + ", ";

	info_out += "\"total_data_output\": ";
	info_out += std::to_string((uint64_t)obs_output_get_total_bytes(stream_output) / (1024.0 * 1024.0)) + ", ";

	info_out += "\"total_stream_time\": ";
	info_out += std::to_string(get_output_duration(stream_output) / 1000) + ", ";

	info_out += "\"average_frame_render_time\": ";
	info_out += std::to_string((double)obs_get_average_frame_time_ns() / 1000000.0) + ", ";

	uint32_t total_frames_r = obs_get_total_frames();
	uint32_t lagged_frames_r = obs_get_lagged_frames();

	info_out += "\"total_frames_rendered\": ";
	info_out += std::to_string(total_frames_r) + ", ";

	info_out += "\"render_frames_missed_num\": ";
	info_out += std::to_string(lagged_frames_r) + ", ";

	info_out += "\"render_frames_missed_percent\": ";
	//count percents then symbols after dot on 3rd value will be cutted
	double render_missed_percent = (double)lagged_frames_r / (double)total_frames_r * 100.0;
	if (isnan(render_missed_percent) || render_missed_percent < DBL_MIN || render_missed_percent > DBL_MAX - 1.0) render_missed_percent = 0;
	info_out += std::to_string(render_missed_percent) + ", ";

	int total_frames_s = obs_output_get_total_frames(stream_output);
	int dropped_frames_s = obs_output_get_frames_dropped(stream_output);

	info_out += "\"total_frames_sent\": ";
	info_out += std::to_string(total_frames_s) + ", ";

	info_out += "\"dropped_frames_num\": ";
	info_out += std::to_string(dropped_frames_s) + ", ";

	info_out += "\"dropped_frames_percent\": ";
	double dropped_frames_percent = (double)dropped_frames_s / (double)total_frames_s * 100.0;
	if (isnan(dropped_frames_percent) || dropped_frames_percent < DBL_MIN || dropped_frames_percent > DBL_MAX - 1.0) dropped_frames_percent = 0;
	info_out += std::to_string(dropped_frames_percent) + ", ";

	info_out += "\"encoder_frames_missed_num\": ";
	info_out += std::to_string(video_output_get_skipped_frames(video)) + ", ";

	info_out += "\"encoder_frames_missed_percent\": ";
	double encoder_frames_missed_percent = round((double)((long double)video_output_get_total_frames(video) / (video_output_get_skipped_frames(video) * 100.0)) * 1000.0) / 1000.0;
	if (isnan(encoder_frames_missed_percent) || encoder_frames_missed_percent < 0.001 || encoder_frames_missed_percent > DBL_MAX - 1.0) encoder_frames_missed_percent = 0;
	info_out += std::to_string(encoder_frames_missed_percent) + ", ";

	info_out += "\"cpu_usage\": ";
	info_out += std::to_string(cpu_usage) + ", ";

	info_out += "\"mem_usage\": ";
	info_out += std::to_string((double)os_get_proc_resident_size() / (1024.0 * 1024.0));
}

bool timer_tick()
{
	if (timer + delay > std::chrono::high_resolution_clock::now())
	{
		return false;
	}
	else
	{
		timer = std::chrono::high_resolution_clock::now();
		return true;
	}
}

int send_data(std::string& message, std::string &out_url)
{
	std::scoped_lock locker(send_lock);

	int attempts = 0;

	CURLcode res;
	res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt(curl, CURLOPT_URL, out_url.c_str());
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, message.c_str());

	if (curl)
	{
		res = curl_easy_perform(curl);
	}

	while (res && attempts < 2) // trying to send a message 2 additional times
	{
		res = curl_easy_perform(curl);
		attempts++;
		std::this_thread::sleep_for(std::chrono::milliseconds(200)); //delay between the attempts
	}

	if (res)
	{
		//curl_easy_cleanup(curl); // we dont need to close the socket at unuccessful semd
		blog(LOG_INFO, "Cannot send a cURL message");
	}

	return 0;
}

void lde_stats_settings(std::string& info_out)
{
	std::string addit_message;

	addit_message += "{ \"type\": \"lbs_stats_settings\", ";

	addit_message += "\"session_id\": \"";
	addit_message += generated_uuid + "\", ";

	info_out += addit_message;

	info_out += "\"stream_output_destination\": { ";
	gen_destination(info_out);
	info_out += " }, ";

	std::string second_part;
	gen_application_settings(second_part);
	second_part += ", ";

	gen_encoder_settings(second_part);

	second_part += " }";

	info_out += second_part;

	addit_message += second_part;

	blog(LOG_INFO, "%s", addit_message.c_str());
}

void lde_stats_stream(std::string& info_out)
{
	std::string addit_message;

	addit_message += "{ \"type\": \"lbs_stats_stream\", ";

	addit_message += "\"session_id\": \"";
	addit_message += generated_uuid + "\", ";

	info_out += addit_message;

	info_out += "\"stream_output_destination\": { ";
	gen_destination(info_out);
	info_out += " }, ";

	std::string second_part;
	second_part += "\"stream_output_stats\": { ";

	gen_stats(second_part);
	second_part += " }, ";

	second_part += "\"stream_output_currentscene\": { ";
	gen_current_scene(second_part);

	second_part += " }, ";

	second_part += "\"encoder_settings.video.bitrate_target\": ";
	gen_encoder_video_bitrate(second_part);
	second_part += ", ";

	second_part += "\"encoder_settings.audio.bitrate_target\": ";
	gen_encoder_audio_bitrate(second_part);

	second_part += " }";

	info_out += second_part;

	addit_message += second_part;

	blog(LOG_INFO, "%s", addit_message.c_str());
}

void lde_stats_status(std::string& info_out, bool is_starting)
{
	std::string addit_message;

	addit_message += "{ \"type\": \"lbs_stats_status\", ";

	addit_message += "\"session_id\": \"";
	addit_message += generated_uuid + "\", ";

	info_out += addit_message;

	info_out += "\"stream_output_destination\": { ";
	gen_destination(info_out);
	info_out += " }, ";

	std::string second_part;
	second_part += "\"stream_output_status\": { ";
	gen_status(second_part, is_starting);
	second_part += " } }";

	info_out += second_part;

	addit_message += second_part;

	blog(LOG_INFO, "%s", addit_message.c_str());
}

void lde_stats_system(std::string& info_out)
{
	std::string addit_message;

	addit_message += "{ \"type\": \"lbs_stats_system\", ";

	addit_message += "\"session_id\": \"";
	addit_message += generated_uuid + "\", ";

	info_out += addit_message;

	info_out += "\"stream_output_destination\": { ";
	gen_destination(info_out);
	info_out += " }, ";

	std::string second_part;
	gen_system_specs(second_part);
	second_part += ", ";

	gen_lde_version(second_part);
	second_part += " }";

	info_out += second_part;

	addit_message += second_part;

	blog(LOG_INFO, "%s", addit_message.c_str());
}

static void bg_send_application_launch()
{
    std::string message;

    lde_stats_system(message);
    send_data(message, url);
}

void send_application_launch()
{
    std::thread bgThread(bg_send_application_launch);
    bgThread.detach();
}

void send_streaming_tick()
{
	if (!obs_frontend_streaming_active())
		return;
	if (!timer_tick())
		return;

	std::string message;

	lde_stats_stream(message);

	send_data(message, url);
}

void stream_stats_updater()
{
	updating = true;

	std::unique_lock<std::mutex> lck(waiter);

	while (updating)
	{
		if (stream_active)
			send_streaming_tick();

		std::this_thread::sleep_for(std::chrono::seconds(1));

		if (!stream_active) cond_waiter.wait(lck);
	}
}

void send_streaming_start()
{
	stream_active = true;

	if (updating) cond_waiter.notify_all();

	std::string message;

	lde_stats_settings(message);
	send_data(message, url);
	message.clear();

	lde_stats_stream(message);
	send_data(message, url);
	message.clear();

	lde_stats_status(message, true);
	send_data(message, url);

	if (!updating)
		updater = std::thread{ stream_stats_updater };
}

void send_streaming_stop()
{
	stream_active = false;
	std::string message;

	lde_stats_status(message, false);

	send_data(message, url);
}

void send_destroy()
{
	if (updating)
	{
		updating = false;
		stream_active = true; // to unlock the locker

		cond_waiter.notify_all();
		updater.join();
	}

	curl_slist_free_all(headers);
	curl_easy_reset(curl);
	curl_easy_cleanup(curl);
	curl_global_cleanup();


	if (cpu_usage_info)
		os_cpu_usage_info_destroy(cpu_usage_info);
}
