#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// standard imports
#include <iostream> 
#include <string> 
#include <vector> 
#include <cmath> 
#include <utility>
#include <thread> 
#include <chrono> 

static float g_peak_db = -100.0f;
static float g_rms_db  = -100.0f;
const char* DEFAULT_AUDIO_FILEPATH = "assets/spin.mp3"; 
const float MINIMUM_SPIN_THRESHOLD = -100.0f; 

const float r1 = 1.0f; 
const float r2 = 2.0f; 
const int screen_width = 130; 
const int screen_height = (int)(screen_width / 2.5);  
const float K2 = 5.0f; // the distance of the donut from the viewer 
const float K1 = screen_width * K2 * 3 / (8 * (r1 + r2)); // functionally the distance that we are from the screen 
const float baseSpeedA = 0.08f; 
const float baseSpeedB = 0.04f;

// convert a 3d point xyz to a 2d-viewable point
std::pair<int, int> projectPointTo2D(float x, float y, float zReciprocal) {
    int newX = (int)((screen_width / 2) + (K1 * zReciprocal * x));
    int newY = (int)((screen_height / 2) - (0.5f * K1 * zReciprocal * y));
    return {newX, newY}; 
}

// convert linear audio amount to decibel float
float to_db(float linear)
{
    if (linear < 1e-7f) return -140.0f;   
    return 20.0f * log10f(linear);
}

// draw the donut and then project it to 2D to be viewable on the screen. zbuffer is used for brightness calculation. 
void drawDonut(float a, float b, std::vector<std::vector<float>>& zbuffer, std::vector<std::vector<char>>& output) {
    float cosa = cos(a); float sina = sin(a); 
    float cosb = cos(b); float sinb = sin(b); 

    const float deltaTheta = 0.07;
    const float deltaPhi = 0.02; 

    for (float theta = 0; theta < 6.28; theta += deltaTheta) {
        float cosTheta = cos(theta); float sinTheta = sin(theta); 

        float circleX = r1 * cosTheta + r2; 
        float circleY = r1 * sinTheta; 

        for (float phi = 0; phi < 6.28; phi += deltaPhi) {
            float cosPhi = cos(phi); float sinPhi = sin(phi); 

            float x = circleX * (cosb * cosPhi + sina * sinb * sinPhi) - (circleY * cosa * sinb);
            float y = circleX * (sinb * cosPhi - sina * cosb * sinPhi) + (circleY * cosa * cosb);
            float z = K2 + (cosa * circleX * sinPhi) + (circleY * sina); 
            float zReciprocal = 1 / z; 

            std::pair<int, int> projectedPoint = projectPointTo2D(x, y, zReciprocal); 

            // i did not derive this myself you got me effed up if u think im doing allat math  
            float L = cosPhi * cosTheta * sinb - cosa * cosTheta * sinPhi - sina*sinTheta + cosb*(cosa*sinTheta - cosTheta*sina*sinPhi);

            if (L > 0) {
                int xp = projectedPoint.first; int yp = projectedPoint.second; 
                if (xp < 0 || xp >= screen_width || yp < 0 || yp >= screen_height) continue;
                if (zReciprocal > zbuffer[yp][xp]) {
                    zbuffer[yp][xp] = zReciprocal; 
                    int luminance = L * 8; 
                    output[yp][xp] = ".,-~:;=!*#$@"[luminance];
                }
            }
        }
    }
}

void printOutputGrid(std::vector<std::vector<char>>& output) {
    // print output grid 
    for (int i = 0; i < output.size(); i++) {
        for (int j = 0; j < output[0].size(); j++) {
            std::cout << output[i][j]; 
        }
        std::cout << '\n'; 
    }

    std::cout.flush(); 
}

// stole this straight from the miniaudio examples lmao
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    ma_decoder* pDecoder = (ma_decoder*)pDevice->pUserData;
    if (pDecoder == NULL) {
        return;
    }

    // honors the looping flag, unlike ma_decoder_read_pcm_frames
    ma_data_source_read_pcm_frames(pDecoder, pOutput, frameCount, NULL);

    const float* samples = (const float*)pOutput;
    ma_uint32 channels   = pDevice->playback.channels;
    ma_uint32 total      = frameCount * channels;

    float peak = 0.0f;
    double sumSq = 0.0;

    for (ma_uint32 i = 0; i < total; ++i) {
        float s = fabsf(samples[i]);
        if (s > peak) peak = s;
        sumSq += (double)s * s;
    }

    float rms = (float)sqrt(sumSq / total);

    g_peak_db = to_db(peak);
    g_rms_db  = to_db(rms);
}

// helper function to abstract miniaudio logic away from the main function of this program 
void setupMiniaudio(ma_device_config* config, ma_device* device, ma_decoder* decoder, ma_result* result, const char* filepath) {
    *result = ma_decoder_init_file(filepath, nullptr, decoder);
    if (*result != MA_SUCCESS) {
        std::cerr << "Error: Audio file decoding failed." << std::endl; 
        exit(1);  
    }

    // sets the audio as looping
    ma_data_source_set_looping(decoder, true); 

    config->playback.format = decoder->outputFormat;
    config->playback.channels = decoder->outputChannels; 
    config->sampleRate = decoder->outputSampleRate; 
    config->dataCallback = data_callback; 
    config->pUserData = decoder; 

    if (ma_device_init(NULL, config, device) != MA_SUCCESS) {
        std::cout << "Error: Failed to initialize audio device" << std::endl; 
        exit(1); 
    }

    if (ma_device_start(device) != MA_SUCCESS) { // The device is sleeping by default so you'll need to start it manually.
        std::cout << "Error: Failed to start audio device" << std::endl; 
        exit(1); 
    }
}

int main(int argc, char* argv[]) {
    // miniaudio mem allocations 
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    ma_device device;
    ma_result result; 
    ma_decoder decoder; 

    bool audioEnabled = false; 

    // speed of the donut as a percentage 
    int speedPercentage = 50; 

    // TODO: Allow for users to adjust audio threshold manually 

    // argument handling 
    if (argc > 1) {
        for (size_t i = 1; i < argc; i++) {
            std::string arg = argv[i]; 
            if (arg == "--help") {
                std::cout << "Usage: donut [-a <audio_filepath>] [-s speedPercentage]" << std::endl; 
                std::cout << "------- Usage options -------" << std::endl; 
                std::cout << "-a: Allows for the donut to spin in sync with an audio path. <audio_filepath> is an optional arg, and is by default the path to the oo ee ee aa ee sound." << std::endl;  
                std::cout << "-s: Optional flag that adjusts how quickly the donut turns. Can range from 0 to 100. Default is 50." << std::endl; 
                std::cout << "--help: Lists all flags and usage instructions for this executable. Using this command will terminate the program immediately." << std::endl; 
                return 0; 
            } else if (arg == "-a") {
                audioEnabled = true; 
                if (i == argc - 1 || argv[i + 1][0] == '-') {
                    setupMiniaudio(&config, &device, &decoder, &result, DEFAULT_AUDIO_FILEPATH); // only call in this branch to speed up runtime 
                } else {
                    setupMiniaudio(&config, &device, &decoder, &result, argv[i + 1]); // only call in this branch to speed up runtime
                    i += 2; 
                }
            } else if (arg == "-s") {
                if (i == argc - 1 || argv[i + 1][0] == '-') {
                    std::cerr << "Error: Invalid or malformed argument passed for speed" << std::endl; 
                    return 1; 
                }

                speedPercentage = atoi(argv[i+1]);  

                // make sure it's between 0 and 100
                speedPercentage = std::min(speedPercentage, 100); 
                speedPercentage = std::max(speedPercentage, 0); 

                i += 2; 
            } else {
                std::cerr << "Invalid argument given: " << arg << std::endl; 
                std::cout << "Usage: donut [-a <audio_filepath>]" << std::endl; 
                return 1; 
            }
        }
    } 

    
    float a = 0; 
    float b = 0; 
    float speedMultiplier = (speedPercentage / 100.0f); 

    std::cout << "\x1b[2J";
    while (true) {
        // z-buffer holds the inverse of z (1/z) for each (x,y,z) tuple. this was an optimization since division is more expensive than multiplication.
        std::vector<std::vector<float>> zbuffer(screen_height, std::vector<float>(screen_width, 0)); 
        std::vector<std::vector<char>> output(screen_height, std::vector<char>(screen_width, ' ')); 

        drawDonut(a, b, zbuffer, output); 
        std::cout << "\x1b[2J";
        printOutputGrid(output); 

        if (!audioEnabled || g_rms_db > MINIMUM_SPIN_THRESHOLD) { // only spin if the audio is above the threshold, or if audio is not enabled
            a += (baseSpeedA * speedMultiplier); 
            b += (baseSpeedB * speedMultiplier); 
        } 
        std::this_thread::sleep_for(std::chrono::milliseconds(40)); // #include <thread>, <chrono>
    }

    // ****** MACRO LEVEL OVERVIEW OF PROGRAM******
    // represent the donut as a vector of 3d points 

    // we get the donut points by looping through sin and theta 
    // and appending the result tuple

    // every frame we convert the 3d points to "2d" points, and then draw them
    // onto the output grid

    // then we rotate around the x-axis by A degrees
    // then we rotate around the z-axis by B degrees
    // next frame 


    // this will never be called right now. added this in case i decide to allow for 
    // async input / non-infinite running time 
    ma_device_uninit(&device);
    ma_decoder_uninit(&decoder);
    return 0; 
}