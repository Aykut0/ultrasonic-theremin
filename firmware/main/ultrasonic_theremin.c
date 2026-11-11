/**
 * Ultrasonic Theremin for ESP32 using ESP-IDF
 * Updated to use lcdgfx library for SH1106 OLED display
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/dac_oneshot.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "ultrasonic.h"


extern void display_draw_frame(uint16_t phase_px,
                               const char *note,
                               float freq_hz,
                               float volume_0_1);



// Include lcdgfx headers with C++ compatibility
#ifdef __cplusplus
extern "C" {
#endif

// OLED Display functions (implemented in display_wrapper.cpp)
void* display_init(void);
void display_clear(void* display);
void display_draw_text(void* display, int x, int y, const char* text, int size);
void display_draw_line(void* display, int x1, int y1, int x2, int y2);
void display_update(void* display);
void display_destroy(void* display);

#ifdef __cplusplus
}
#endif

// Pin definitions
#define PITCH_TRIG_PIN    GPIO_NUM_17
#define PITCH_ECHO_PIN    GPIO_NUM_16
#define VOLUME_TRIG_PIN   GPIO_NUM_5  
#define VOLUME_ECHO_PIN   GPIO_NUM_18
#define AUDIO_OUT_PIN     GPIO_NUM_25  // DAC_CHAN_0

// Alternative pins for volume sensor (if GPIO 5/18 don't work)
// #define VOLUME_TRIG_PIN   GPIO_NUM_19
// #define VOLUME_ECHO_PIN   GPIO_NUM_23

// I2C pins for OLED
#define I2C_MASTER_SCL_IO GPIO_NUM_22
#define I2C_MASTER_SDA_IO GPIO_NUM_21
#define I2C_MASTER_NUM    I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 800000

// Distance range settings (in cm)
#define MIN_DISTANCE      10.0
#define MAX_DISTANCE      50.0

// Audio settings
#define SAMPLE_RATE       22050
#define SINE_TABLE_SIZE   256

static const char *TAG = "THEREMIN";
char currentNoteName[4] = "A4";   

// Note frequencies (C4 to C6 - 2 octaves)
const float notes[] = {
    261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00,
    415.30, 440.00, 466.16, 493.88, 523.25, 554.37, 587.33, 622.25,
    659.25, 698.46, 739.99, 783.99, 830.61, 880.00, 932.33, 987.77,
    1046.50
};
const int NUM_NOTES = sizeof(notes) / sizeof(notes[0]);

// Global variables
static uint8_t sineTable[SINE_TABLE_SIZE];
static volatile float currentFreq = 440.0;
static volatile float currentVolume = 0.5;
static volatile bool audioActive = true;
static dac_oneshot_handle_t dac_handle;

// Display handle
static void* display_handle = NULL;

// Task handles
static TaskHandle_t sensorTaskHandle;
static TaskHandle_t audioTaskHandle;
static TaskHandle_t displayTaskHandle;

// Ultrasonic sensors
static ultrasonic_sensor_t pitch_sensor = {
    .trigger_pin = PITCH_TRIG_PIN,
    .echo_pin = PITCH_ECHO_PIN
};

static ultrasonic_sensor_t volume_sensor = {
    .trigger_pin = VOLUME_TRIG_PIN,
    .echo_pin = VOLUME_ECHO_PIN
};

// Function prototypes
static void generateSineTable(void);
static float mapRange(float value, float in_min, float in_max, float out_min, float out_max);
static int findNearestNote(float mappedValue);
static void sensorTask(void *parameter);
static void audioTask(void *parameter);
static void displayTask(void *parameter);
static esp_err_t i2c_master_init(void);
static const char* getNoteName(int noteIndex);

void app_main(void) {
    ESP_LOGI(TAG, "Starting Ultrasonic Theremin with SH1106 Display");
    
    // Initialize I2C for OLED
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized");
    
    // Initialize display
    display_handle = display_init();
    if (display_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display!");
    } else {
        ESP_LOGI(TAG, "Display initialized successfully");
        
        // Show startup screen
        display_clear(display_handle);
        display_draw_text(display_handle, 25, 20, "THEREMIN", 2);
        display_draw_text(display_handle, 45, 40, "v1.0", 1);
        display_update(display_handle);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    
    // Initialize ultrasonic sensors
    esp_err_t res;
    res = ultrasonic_init(&pitch_sensor);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing pitch sensor: %d", res);
    }
    
    res = ultrasonic_init(&volume_sensor);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Error initializing volume sensor: %d", res);
    }
    
    // Generate sine wave lookup table
    generateSineTable();
    
    // Setup DAC for audio output
    dac_oneshot_config_t dac_cfg = {
        .chan_id = DAC_CHAN_0,
    };
    ESP_ERROR_CHECK(dac_oneshot_new_channel(&dac_cfg, &dac_handle));
    ESP_ERROR_CHECK(dac_oneshot_output_voltage(dac_handle, 128)); // Start at center
    
    // Create tasks
    xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 2, &sensorTaskHandle, 0);
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 3, &audioTaskHandle, 1);
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", 4096, NULL, 1, &displayTaskHandle, 0);
    
    ESP_LOGI(TAG, "Ultrasonic Theremin Started!");
    
    // Main loop
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void generateSineTable(void) {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        float angle = (2.0 * M_PI * i) / SINE_TABLE_SIZE;
        sineTable[i] = (uint8_t)((sin(angle) + 1.0) * 127.5);
    }
}

static float mapRange(float value, float in_min, float in_max, float out_min, float out_max) {
    if (value < in_min) value = in_min;
    if (value > in_max) value = in_max;
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static int findNearestNote(float mappedValue) {
    int noteIndex = (int)(mappedValue * (NUM_NOTES - 1));
    if (noteIndex < 0) noteIndex = 0;
    if (noteIndex >= NUM_NOTES) noteIndex = NUM_NOTES - 1;
    return noteIndex;
}

static void sensorTask(void *parameter) {
    const TickType_t taskDelay = pdMS_TO_TICKS(50);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Starting Sensor Task");
    
    uint32_t pitch_distance_cm = 0;
    uint32_t volume_distance_cm = 0;
    esp_err_t pitch_res;
    esp_err_t volume_res;
    
    // Moving average filters
    const int filterSize = 5;
    float pitch_buffer[filterSize];
    float volume_buffer[filterSize];
    int filter_index = 0;
    
    // Initialize buffers
    for (int i = 0; i < filterSize; i++) {
        pitch_buffer[i] = 20.0;
        volume_buffer[i] = 20.0;
    }
    
    while (1) {
        // Read pitch sensor
        pitch_res = ultrasonic_measure_cm(&pitch_sensor, MAX_DISTANCE, &pitch_distance_cm);
        
        // Small delay between sensors
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Read volume sensor
        volume_res = ultrasonic_measure_cm(&volume_sensor, MAX_DISTANCE, &volume_distance_cm);
        
        // Update buffers
        if (pitch_res == ESP_OK) {
            pitch_buffer[filter_index] = (float)pitch_distance_cm;
        }
        
        if (volume_res == ESP_OK) {
            volume_buffer[filter_index] = (float)volume_distance_cm;
        }
        
        filter_index = (filter_index + 1) % filterSize;
        
        // Calculate averages
        float avg_pitch = 0, avg_volume = 0;
        for (int i = 0; i < filterSize; i++) {
            avg_pitch += pitch_buffer[i];
            avg_volume += volume_buffer[i];
        }
        avg_pitch /= filterSize;
        avg_volume /= filterSize;
        
        // Map to frequency and volume
        if (pitch_res == ESP_OK && pitch_distance_cm <= MAX_DISTANCE) {
            float pitch_mapped = mapRange(avg_pitch, MIN_DISTANCE, MAX_DISTANCE, 1.0, 0.0);
            int noteIndex = findNearestNote(pitch_mapped);
            currentFreq = notes[noteIndex];
            snprintf(currentNoteName, sizeof currentNoteName, "%s", getNoteName(noteIndex));
            audioActive = true;  // Hand detected, enable audio
        } 
        else {
            // No hand or sensor error - go silent
            audioActive = false;
            strcpy(currentNoteName, "---");
        }
        
        if (volume_res == ESP_OK) {
            float linear_vol = mapRange(avg_volume, MIN_DISTANCE, MAX_DISTANCE, 1.0, 0.0);
            currentVolume = linear_vol * linear_vol;
        }
        
        vTaskDelay(taskDelay);
    }
}

static void audioTask(void *parameter) {
    float phase = 0;
    const int samplePeriodUs = 45; // ~22kHz
    
    ESP_LOGI(TAG, "Starting Audio Task");
    
    while (1) {
        if (audioActive) {
            // Generate sine wave sample
            int tableIndex = (int)phase % SINE_TABLE_SIZE;
            uint8_t baseSample = sineTable[tableIndex];
            
            // Apply volume
            float scaledSample = 128.0 + (baseSample - 128.0) * currentVolume;
            uint8_t outputSample = (uint8_t)scaledSample;
            
            dac_oneshot_output_voltage(dac_handle, outputSample);
            
            // Update phase
            float phaseInc = (currentFreq * SINE_TABLE_SIZE) / SAMPLE_RATE;
            phase += phaseInc;
            if (phase >= SINE_TABLE_SIZE) {
                phase -= SINE_TABLE_SIZE;
            }
        } else {
            dac_oneshot_output_voltage(dac_handle, 128);
            phase = 0;
        }
        
        esp_rom_delay_us(samplePeriodUs);
        
        // Yield periodically
        static int yieldCounter = 0;
        if (++yieldCounter >= 500) {
            vTaskDelay(1);
            yieldCounter = 0;
        }
    }
}

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static const char* getNoteName(int noteIndex) {
    const char* noteNames[] = {
        "C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
        "C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5",
        "C6"
    };
    
    if (noteIndex >= 0 && noteIndex < NUM_NOTES) {
        return noteNames[noteIndex];
    }
    return "---";
}

static void displayTask(void *arg)
{
    uint16_t phase = 0;
    const TickType_t frameDelay = pdMS_TO_TICKS(50); // 20 FPS

    while (1) {
        float cycles = currentFreq / 440.0f;
        display_draw_frame(phase, currentNoteName, currentFreq, currentVolume);
        phase += (uint16_t)(cycles * 4);   // ≈ 4 px per 440 Hz
        phase &= 0xFFFF;                   // prevent overflow
        vTaskDelay(frameDelay);
    }
}