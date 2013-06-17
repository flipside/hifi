//
//  Webcam.cpp
//  interface
//
//  Created by Andrzej Kapolka on 6/17/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.

#include <opencv2/opencv.hpp>

#include <Log.h>

#include "Webcam.h"

Webcam::Webcam() {
    if ((_capture = cvCaptureFromCAM(-1)) == 0) {
        printLog("Failed to open webcam.\n");
        return;
    }
    
    // bump up the capture property
    cvSetCaptureProperty(_capture, CV_CAP_PROP_FPS, 60);
    
    // get the dimensions of the frames
    _frameWidth = cvGetCaptureProperty(_capture, CV_CAP_PROP_FRAME_WIDTH);
    _frameHeight = cvGetCaptureProperty(_capture, CV_CAP_PROP_FRAME_HEIGHT);
    
    // initialize the texture that will contain the grabbed frames
    glGenTextures(1, &_frameTextureID);
    glBindTexture(GL_TEXTURE_2D, _frameTextureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, _frameWidth, _frameHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

Webcam::~Webcam() {
    if (_capture != 0) {
        cvReleaseCapture(&_capture);
    }
}

void Webcam::grabFrame() {
    IplImage* image = cvQueryFrame(_capture);
    if (image == 0) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, _frameTextureID);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, _frameWidth, _frameHeight, GL_RGB, GL_UNSIGNED_BYTE, image->imageData);
    glBindTexture(GL_TEXTURE_2D, 0);
}
