# FFMpegAudioPlayer
 A Simple ffmpeg music player with SDL1 or SDL2 implemented in C/C++ 
 
 I was trying to find a good working example of a music player using the library ffmpeg, but I coldn't find any working properly with a sort of different music files.
 Luckily I found a source code from Lei Xiaohua, who implemented a basic version, but lacked of a good file format handling and buffering behaviour. If you want more information about his source you can go to this page: https://titanwolf.org/Network/Articles/Article?AID=95bb99f1-97a1-4a28-afed-809aa45f9bd2 or as stated in his source: http://blog.csdn.net/leixiaohua1020
 
 It's very basic. It only plays an audio file passed as a parameter until the end. Nothing more. 
 
 V1.0 First Version. Modifications done to the original code:
 - Output frequency and samples set by default with constants. The samples number is computed to not stress too much the audio device. The previous code, had the samples selected with the source file samples. Now it doesn't matter the audio format. It will play whitout choppiness.
 - Corrected error when calling to SDL_OpenAudio in SDL2. Now it works well in SDL1 and SDL2
 - Implemmented a ring buffer to store the audio in a more convenient way. 
 - Updated all deprecated ffmpeg code