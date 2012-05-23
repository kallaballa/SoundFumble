SoundFumble
===========

SoundFumble is a gimp plugin that renders images to sound.
It does it by writing the image directly to an alsa device. You can live edit the images during playback (Note: With Gimp 2.8 you can also make full use of alpha blending and layers).
PCM playback settings are configurable.

#### Install

##### Prerequisites

* libgimp2.0-dev
* libasound2-dev 

##### Build

    git clone https://github.com/kallaballa/SoundFumble.git
    cd SoundFumble
    make
    make install

