////////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//

//
// The code has been contributed by Arkadiusz Raj on 2016 Oct
//

#include "precomp.hpp"

#ifdef HAVE_ARAVIS_API

#include <arv.h>

//  Supported types of data:
//      video/x-raw, fourcc:'GRAY'  -> 8bit, 1 channel
//      video/x-raw, fourcc:'Y12 '  -> 12bit, 1 channel

#define MODE_GRAY8  CV_FOURCC_MACRO('G','R','E','Y')
#define MODE_GRAY12 CV_FOURCC_MACRO('Y','1','2',' ')

#define BETWEEN(a,b,c) ((a) < (b) ? (b) : ((a) > (c) ? (c) : (a) ))

/********************* Capturing video from camera via Aravis *********************/

class CvCaptureCAM_Aravis : public CvCapture
{
public:
    CvCaptureCAM_Aravis();
    virtual ~CvCaptureCAM_Aravis()
    {
        close();
    }

    virtual bool open(int);
    virtual void close();
    virtual double getProperty(int) const;
    virtual bool setProperty(int, double);
    virtual bool grabFrame();
    virtual IplImage* retrieveFrame(int);
    virtual int getCaptureDomain()
    {
        return CV_CAP_ARAVIS;
    }

protected:
    bool create(int);
    bool init_buffers();

    void stopCapture();
    bool startCapture();

    bool getDeviceNameById(int id, std::string &device);

    ArvCamera       *camera;                // Camera to control.
    ArvStream       *stream;                // Object for video stream reception.
    void            *framebuffer;           //

    unsigned int    payload;                // Width x height x Pixel width.

    int             widthMin;               // Camera sensor minium width.
    int             widthMax;               // Camera sensor maximum width.
    int             heightMin;              // Camera sensor minium height.
    int             heightMax;              // Camera sensor maximum height.
    bool            fpsAvailable;
    double          fpsMin;                 // Camera minium fps.
    double          fpsMax;                 // Camera maximum fps.
    bool            gainAvailable;
    double          gainMin;                // Camera minimum gain.
    double          gainMax;                // Camera maximum gain.
    bool            exposureAvailable;
    double          exposureMin;            // Camera's minimum exposure time.
    double          exposureMax;            // Camera's maximum exposure time.
    gint64          *pixelFormats;
    guint           pixelFormatsCnt;


    int             num_buffers;            // number of payload transmission buffers

    ArvPixelFormat  pixelFormat;            // current pixel format
    int             width;                  // current width of frame
    int             height;                 // current height of image
    double          fps;                    // current fps
    double          exposure;               // current value of exposure time
    double          gain;                   // current value of gain

    IplImage        *frame;                 // local frame copy
};


CvCaptureCAM_Aravis::CvCaptureCAM_Aravis()
{
    camera = NULL;
    stream = NULL;
    framebuffer = NULL;

    payload = 0;

    widthMin = widthMax = heightMin = heightMax = 0;
    fpsMin = fpsMax = gainMin = gainMax = exposureMin = exposureMax = 0;

    num_buffers = 50;
    frame = NULL;
}

void CvCaptureCAM_Aravis::close()
{
    stopCapture();
}

bool CvCaptureCAM_Aravis::getDeviceNameById(int id, std::string &device)
{
    arv_update_device_list();

    int n_devices = arv_get_n_devices();

    for(int i = 0; i< n_devices; i++){
        if(id == i){
            device = arv_get_device_id(i);
            return true;
        }
    }

    return false;
}

bool CvCaptureCAM_Aravis::create( int index )
{
    std::string deviceName;
    if(!getDeviceNameById(index, deviceName))
        return false;

    return NULL != (camera = arv_camera_new(deviceName.c_str()));
}

bool CvCaptureCAM_Aravis::init_buffers()
{
    if (stream) {
        g_object_unref(stream);
        stream = NULL;
    }
    if( (stream = arv_camera_create_stream(camera, NULL, NULL)) ) {
        g_object_set(stream,
            "socket-buffer", ARV_GV_STREAM_SOCKET_BUFFER_AUTO,
            "socket-buffer-size", 0, NULL);
        g_object_set(stream,
            "packet-resend", ARV_GV_STREAM_PACKET_RESEND_NEVER, NULL);
        g_object_set(stream,
            "packet-timeout", (unsigned) 40000,
            "frame-retention", (unsigned) 200000, NULL);

        payload = arv_camera_get_payload (camera);

        for (int i = 0; i < num_buffers; i++)
            arv_stream_push_buffer(stream, arv_buffer_new(payload, NULL));

        return true;
    }

    return false;
}

bool CvCaptureCAM_Aravis::open( int index )
{
    if( create( index) ) {
        // fetch properties bounds
        pixelFormats = arv_camera_get_available_pixel_formats(camera, &pixelFormatsCnt);

        arv_camera_get_width_bounds(camera, &widthMin, &widthMax);
        arv_camera_get_height_bounds(camera, &heightMin, &heightMax);
        arv_camera_set_region(camera, 0, 0, widthMax, heightMax);

        if( (fpsAvailable = arv_camera_is_frame_rate_available(camera)) )
            arv_camera_get_frame_rate_bounds(camera, &fpsMin, &fpsMax);
        if( (gainAvailable = arv_camera_is_gain_available(camera)) )
            arv_camera_get_gain_bounds (camera, &gainMin, &gainMax);
        if( (exposureAvailable = arv_camera_is_exposure_time_available(camera)) )
            arv_camera_get_exposure_time_bounds (camera, &exposureMin, &exposureMax);

        // get initial values
        pixelFormat = arv_camera_get_pixel_format(camera);
        exposure = arv_camera_get_exposure_time(camera);
        gain = arv_camera_get_gain(camera);
        fps = arv_camera_get_frame_rate(camera);

        return startCapture();
    }
    return false;
}

bool CvCaptureCAM_Aravis::grabFrame()
{
    ArvBuffer *arv_buffer = NULL;
    int max_tries = 10;
    int tries = 0;
    for(; tries < max_tries; tries ++) {
        arv_buffer = arv_stream_timeout_pop_buffer (stream, 200000);
        if (arv_buffer != NULL && arv_buffer_get_status (arv_buffer) != ARV_BUFFER_STATUS_SUCCESS) {
            arv_stream_push_buffer (stream, arv_buffer);
        } else break;
    }

    if (tries == max_tries)
        return false;

    size_t buffer_size;
    framebuffer = (void*)arv_buffer_get_data (arv_buffer, &buffer_size);

    arv_buffer_get_image_region (arv_buffer, NULL, NULL, &width, &height);

    arv_stream_push_buffer(stream, arv_buffer);
    return true;
}

IplImage* CvCaptureCAM_Aravis::retrieveFrame(int)
{
    if(framebuffer) {
        int depth = 0, channels = 0;
        switch(pixelFormat) {
            case ARV_PIXEL_FORMAT_MONO_8:
                depth = IPL_DEPTH_8U;
                channels = 1;
                break;
            case ARV_PIXEL_FORMAT_MONO_12:
                depth = IPL_DEPTH_16U;
                channels = 1;
                break;
        }
        if(depth && channels) {
            IplImage src;
            cvInitImageHeader( &src, cvSize( width, height ), depth, channels, IPL_ORIGIN_TL, 4 );

            cvSetData( &src, framebuffer, src.widthStep );
            if( !frame ||
                 frame->width != src.width ||
                 frame->height != src.height ||
                 frame->depth != src.depth ||
                 frame->nChannels != src.nChannels) {

                cvReleaseImage( &frame );
                frame = cvCreateImage( cvGetSize(&src), src.depth, channels );
            }
            cvCopy(&src, frame);

            return frame;
        }
    }
    return NULL;
}

double CvCaptureCAM_Aravis::getProperty( int property_id ) const
{
    switch ( property_id ) {
        case CV_CAP_PROP_EXPOSURE:
            if(exposureAvailable) {
                /* exposure time in seconds, like 1/100 s */
                return arv_camera_get_exposure_time(camera) / 1e6;
            }
            break;

        case CV_CAP_PROP_FPS:
            if(fpsAvailable) {
                return arv_camera_get_frame_rate(camera);
            }
            break;

        case CV_CAP_PROP_GAIN:
            if(gainAvailable) {
                return arv_camera_get_gain(camera);
            }
            break;

        case CV_CAP_PROP_FOURCC:
            {
                ArvPixelFormat currFormat = arv_camera_get_pixel_format(camera);
                switch( currFormat ) {
                    case ARV_PIXEL_FORMAT_MONO_8:
                        return MODE_GRAY8;
                    case ARV_PIXEL_FORMAT_MONO_12:
                        return MODE_GRAY12;
                }
            }
    }
    return -1.0;
}

bool CvCaptureCAM_Aravis::setProperty( int property_id, double value )
{
    switch ( property_id ) {
        case CV_CAP_PROP_EXPOSURE:
            if(exposureAvailable) {
                /* exposure time in seconds, like 1/100 s */
                value *= 1e6; // -> from s to us
                arv_camera_set_exposure_time(camera, exposure = BETWEEN(value, exposureMin, exposureMax));
                break;
            } else return false;

        case CV_CAP_PROP_FPS:
            if(fpsAvailable) {
                arv_camera_set_frame_rate(camera, fps = BETWEEN(value, fpsMin, fpsMax));
                break;
            } else return false;

        case CV_CAP_PROP_GAIN:
            if(gainAvailable) {
                arv_camera_set_gain(camera, gain = BETWEEN(value, gainMin, gainMax));
                break;
            } else return false;

        case CV_CAP_PROP_FOURCC:
            {
                ArvPixelFormat newFormat = pixelFormat;
                switch((int)value) {
                    case MODE_GRAY8:
                        newFormat = ARV_PIXEL_FORMAT_MONO_8;
                        break;
                    case MODE_GRAY12:
                        newFormat = ARV_PIXEL_FORMAT_MONO_12;
                        break;
                }
                if(newFormat != pixelFormat) {
                    stopCapture();
                    arv_camera_set_pixel_format(camera, pixelFormat = newFormat);
                    startCapture();
                }
            }
            break;

        default:
            return false;
    }

    return true;
}

void CvCaptureCAM_Aravis::stopCapture()
{
    arv_camera_stop_acquisition(camera);

    g_object_unref(stream);
    stream = NULL;
}

bool CvCaptureCAM_Aravis::startCapture()
{
    if( init_buffers() ) {
        arv_camera_set_acquisition_mode(camera, ARV_ACQUISITION_MODE_CONTINUOUS);
        arv_device_set_string_feature_value(arv_camera_get_device (camera), "TriggerMode" , "Off");
        arv_camera_start_acquisition(camera);

        return true;
    }
    return false;
}

CvCapture* cvCreateCameraCapture_Aravis( int index )
{
    CvCaptureCAM_Aravis* capture = new CvCaptureCAM_Aravis;

    if ( capture->open( index ))
        return capture;

    delete capture;
    return NULL;
}
#endif
