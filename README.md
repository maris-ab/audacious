First very big thanks to all Audacious developers for creating this wonderful software.

As variety of different music format increases I have added to Audacious DSD format support.
For excellent listening You need external USB DAC to play DSD files in native mode.
By converting them into PCM You are having artifacts from both DSD and PCM coding systems and then Hi-Res music does not sound so nice and transparent.

DSD format is used in SACD discs (Super Audio CD)
It contains Hi-Res Stereo and Multichannel DSD64 format music.
There are also many download services where You can download Hi-Res DSD files.

**DSD Playback**

With this feature You can play DSD files in native and DoP format on DAC supporting DSD.
It is using ffaudio FFmpeg Plugin but You need to enable DSD in it:

[x] Enable DSD stream output

For Linux DSD playback is supported through ALSA and PipeWire Output.
For MacOS it works on CoreAudio output in DoP mode,

You should enable [x] DSD as DoP

and during playing keep Software Volume to maximum and use only hardware volume on Your DAC.
It is because MacOS does not support Native DSD playback.
I have added also DSF file creation in FileWriter Plugin.
Changes works also on Windows, but as playing of DSD is possible only through ASIO or WASAPI, I am planning to make new OutputPlugin for Audacious based on PortAudio library.

I was searching for such music player what can play SACD files on Linux for 10 years and understood that all is only in my hands and found Audacious what can fulfill my wish.
I have reviewed several players and found very complicate code inside.
When I looked inside Audacious I was pleasantly surprised how easy is to understand written code and adapt it to my needs.

**Bit-Perfect audio**

There are many discussions in Internet about Bit-Perfect audio playback.
As current Audacious is using FLOAT32 for internal audio path, it is bit perfect till 24 bit audio.
In my fork I have added possibility to use FLOAT64 for audio path what makes Audacious Bit-Perfect.
Effects plugins also uses FLOAT64 for processing except two are in FLOAT32 - "Sample Rate Converter" and "Speed and Pitch"
ReplayGain and Visualizations are left in FLOAT32, only audio path is changed to FLOAT64.
You can still compile audacious in FLOAT32 mode by changing only one line in src/libaudcore/audio.h.in

Maybe Bit-Perfect audio is more marketing trick as most of the PCM music is digitalized using not more than 24 bits,
But nowadays on 64-bit systems it does not requires additional processing resource, then why not to use it.
On PCM music You will not hear difference, but if Your DAC supports 32-Bit PCM then on converting from DSD to PCM You will not loose your 8 bits.
I found also that using PipeWire with FLOAT64 there are better sound quality when it is converting to default internal 48Khz sample rate from different source rates.
