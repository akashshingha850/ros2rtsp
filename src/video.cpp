#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/app/gstappsrc.h>
#include <rclcpp/rclcpp.hpp>

#include "../include/image2rtsp.hpp"
#include "../include/image_encodings.h"

using namespace std;

static void *mainloop(void *arg){
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);
    g_main_destroy(loop);
    return NULL;
}

void Image2rtsp::video_mainloop_start(){
    pthread_t tloop;
    gst_init(NULL, NULL);
    pthread_create(&tloop, NULL, &mainloop, NULL);
}

GstRTSPServer *Image2rtsp::rtsp_server_create(const std::string &port, const bool local_only){
    GstRTSPServer *server;

    /* create a server instance */
    server = gst_rtsp_server_new();
    // char *port = (char *) port;
    g_object_set(server, "service", port.c_str(), NULL);
    /* attach the server to the default maincontext */
    if (local_only){
    g_object_set(server, "address", "127.0.0.1", NULL);
    }
    gst_rtsp_server_attach(server, NULL);
    /* add a timeout for the session cleanup */
    g_timeout_add_seconds(2, (GSourceFunc)session_cleanup, this);
    return server;
}

void Image2rtsp::rtsp_server_add_url(const char *url, const char *sPipeline){
    GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;

    /* get the mount points for this server, every server has a default object
     * that be used to map uri mount points to media factories */
    mounts = gst_rtsp_server_get_mount_points(rtsp_server);

    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */
    factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, sPipeline);

    /* notify when our media is ready, This is called whenever someone asks for
     * the media and a new pipeline is created */
    // Pass `this` as user_data so media_configure can access node state and push a preroll frame
    g_signal_connect(factory, "media-configure", (GCallback)media_configure, this);

    // Use non-shared media factory so each client gets its own pipeline
    // This avoids prerolling a shared pipeline without available appsrc data
    gst_rtsp_media_factory_set_shared(factory, FALSE);

    /* attach the factory to the url */
    gst_rtsp_mount_points_add_factory(mounts, url, factory);

    /* don't need the ref to the mounts anymore */
    g_object_unref(mounts);
}

static void media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data){
    Image2rtsp *node = static_cast<Image2rtsp*>(user_data);
    GstElement *pipeline = gst_rtsp_media_get_element(media);
    GstElement *imagesrc = gst_bin_get_by_name(GST_BIN(pipeline), "imagesrc");

    if (imagesrc){
        /* store appsrc in node for later pushes */
        node->appsrc = GST_APP_SRC(imagesrc);

        /* instruct appsrc that we will be dealing with timed buffers */
        gst_util_set_object_arg(G_OBJECT(node->appsrc), "format", "time");

        /* mark stream-type to not require preroll and reduce buffering */
        gst_app_src_set_stream_type(node->appsrc, GST_APP_STREAM_TYPE_STREAM);
        gst_app_src_set_max_buffers(node->appsrc, 0);
        gst_app_src_set_max_bytes(node->appsrc, 0);
        gst_app_src_set_max_time(node->appsrc, 0);

        /* create a minimal dummy preroll frame to satisfy pipeline preroll */
        guint width = 2;
        guint height = 2;
        guint fr = node->framerate > 0 ? node->framerate : 30;
        GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                           "format", G_TYPE_STRING, "RGB",
                                           "width", G_TYPE_INT, width,
                                           "height", G_TYPE_INT, height,
                                           "framerate", GST_TYPE_FRACTION, fr, 1,
                                           NULL);
        gst_app_src_set_caps(node->appsrc, caps);

        gsize buf_size = width * height * 3;
        GstBuffer *buf = gst_buffer_new_allocate(NULL, buf_size, NULL);
        GstMapInfo map;
        if (gst_buffer_map(buf, &map, GST_MAP_WRITE)){
            if (map.data) memset(map.data, 64, buf_size);
            gst_buffer_unmap(buf, &map);
        }
        GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_LIVE);
        gst_app_src_push_buffer(node->appsrc, buf);

        gst_caps_unref(caps);
        gst_object_unref(pipeline);
        return;
    } else {
        guint i, n_streams;
        n_streams = gst_rtsp_media_n_streams(media);

        for (i = 0; i < n_streams; i++){
            GstRTSPAddressPool *pool;
            GstRTSPStream *stream;
            gchar *min, *max;

            stream = gst_rtsp_media_get_stream(media, i);

            /* make a new address pool */
            pool = gst_rtsp_address_pool_new();

            min = g_strdup_printf("224.3.0.%d", (2 * i) + 1);
            max = g_strdup_printf("224.3.0.%d", (2 * i) + 2);
            gst_rtsp_address_pool_add_range(pool, min, max, 5000 + (10 * i), 5010 + (10 * i), 1);
            g_free(min);
            g_free(max);
            gst_rtsp_stream_set_address_pool(stream, pool);
            g_object_unref(pool);
        }
    }
}

GstCaps *Image2rtsp::gst_caps_new_from_image(const sensor_msgs::msg::Image::SharedPtr &msg){
    // http://gstreamer.freedesktop.org/data/doc/gstreamer/head/pwg/html/section-types-definitions.html
    static const std::map<std::string, std::string> known_formats = {
        {sensor_msgs::image_encodings::RGB8, "RGB"},
        {sensor_msgs::image_encodings::RGB16, "RGB16"},
        {sensor_msgs::image_encodings::RGBA8, "RGBA"},
        {sensor_msgs::image_encodings::RGBA16, "RGBA16"},
        {sensor_msgs::image_encodings::BGR8, "BGR"},
        {sensor_msgs::image_encodings::BGR16, "BGR16"},
        {sensor_msgs::image_encodings::BGRA8, "BGRA"},
        {sensor_msgs::image_encodings::BGRA16, "BGRA16"},
        {sensor_msgs::image_encodings::MONO8, "GRAY8"},
        {sensor_msgs::image_encodings::MONO16, "GRAY16_LE"},
        {sensor_msgs::image_encodings::YUV422, "YUY2"},
    };

    if (msg->is_bigendian){
        RCLCPP_ERROR(this->get_logger(), "GST: big endian image format is not supported");
        return nullptr;
    }

    auto format = known_formats.find(msg->encoding);
    if (format == known_formats.end()){
        RCLCPP_ERROR(this->get_logger(), "GST: image format '%s' unknown", msg->encoding.c_str());
        return nullptr;
    }

    return gst_caps_new_simple("video/x-raw",
                               "format", G_TYPE_STRING, format->second.c_str(),
                               "width", G_TYPE_INT, msg->width,
                               "height", G_TYPE_INT, msg->height,
                               "framerate", GST_TYPE_FRACTION, framerate, 1,
                               nullptr);
}

static gboolean session_cleanup(Image2rtsp *node, rclcpp::Logger logger, gboolean ignored){
    GstRTSPServer *server = node->rtsp_server;
    GstRTSPSessionPool *pool;
    int num;

    pool = gst_rtsp_server_get_session_pool(server);
    num = gst_rtsp_session_pool_cleanup(pool);
    g_object_unref(pool);

    if (num > 0)
    {
        char s[32];
        snprintf(s, 32, (char *)"Sessions cleaned: %d", num);
        RCLCPP_DEBUG(node->get_logger(), s);
    }
    return TRUE;
}

void Image2rtsp::topic_callback(const sensor_msgs::msg::Image::SharedPtr msg){
    GstBuffer *buf;
    GstCaps *caps; // image properties. see return of Image2rtsp::gst_caps_new_from_image
    char *gst_type, *gst_format = (char *)"";
    if (appsrc != NULL){
        RCLCPP_DEBUG(this->get_logger(), "Received image %dx%d, encoding=%s", msg->width, msg->height, msg->encoding.c_str());
        // Set caps from message
        caps = gst_caps_new_from_image(msg);
        gst_app_src_set_caps(appsrc, caps);
        buf = gst_buffer_new_allocate(nullptr, msg->data.size(), nullptr);
        gst_buffer_fill(buf, 0, msg->data.data(), msg->data.size());
        GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_LIVE);
        gst_app_src_push_buffer(appsrc, buf);
    }
}

void Image2rtsp::compressed_topic_callback(const sensor_msgs::msg::CompressedImage::SharedPtr msg){
    if (appsrc == NULL) return;
    // Decompress the image
    cv::Mat img = cv::imdecode(cv::Mat(msg->data), cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        RCLCPP_ERROR(this->get_logger(), "Failed to decompress image");
        return;
    }

    // Determine the GStreamer caps
    std::string gst_format;
    switch (img.type()) {
        case CV_8UC3: gst_format = "BGR"; break;    // BGR images
        case CV_8UC4: gst_format = "RGBA"; break;   // RGBA images
        case CV_8UC1: gst_format = "GRAY8"; break;  // Grayscale images
        default:
            RCLCPP_ERROR(this->get_logger(), "Unsupported image type");
            return;
    }

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, gst_format.c_str(),
                                        "width", G_TYPE_INT, img.cols,
                                        "height", G_TYPE_INT, img.rows,
                                        "framerate", GST_TYPE_FRACTION, framerate, 1,
                                        nullptr);

    // Set caps on appsrc
    gst_app_src_set_caps(appsrc, caps);
    gst_caps_unref(caps);

    // Create a GstBuffer and fill it with the image data
    GstBuffer *buf = gst_buffer_new_allocate(nullptr, img.total() * img.elemSize(), nullptr);
    gst_buffer_fill(buf, 0, img.data, img.total() * img.elemSize());
    GST_BUFFER_FLAG_SET(buf, GST_BUFFER_FLAG_LIVE);

    // Push the buffer to GStreamer
    gst_app_src_push_buffer(appsrc, buf);
}