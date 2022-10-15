// Project chernobyl
//
// Read temp of boiler out water and boiler in water and if out water is hotter than
// specified temp and in watter is colder than specified minimum close the relay to start pump

#include <Arduino.h>
#include <EEPROM.h>
#include <TM1637Display.h>

// define pin numbers
#define TERM_PIN_C 0
#define TERM_PIN_H 1
#define DISPLAY_CLK 2
#define DISPLAY_DIO 3
#define SSR_PIN 4
#define BUTTON_OTHER 5
#define BUTTON_UP    6
#define BUTTON_DOWN  7

//resistor parameters
#define TERM_NOM 10000    // Reference thermistor resistance
#define REF_TEMP 25       // Temperature for thermistor resistance
#define BETA_FACTOR 3977  // Beta factor
#define RESISTANCE 10000  // Serialy connected resistor resistance

//  AREF     O +3.3V          TERM PIN                 _   GND
//   |       |                   |                    ___
//   |       |                   |                   _____
//   |       |     _________     |     ______/____     |
//   |_______|____|         |____|____|     /     |____|
//                |_________|         |____/______|
//                                    ____/

  
#define BRIGHTNESS_MAX 12
#define BRIGHTNESS_MIN 8

#define FLASH_MAX 3   // in main loop cycles
#define PUMP_STATE_WAIT_MAX 120   // in main loop cycles

#define EEPROM_TEMP_H 0
#define EEPROM_TEMP_C 1

#define BUTTON_COUNT 3
const uint8_t buttons[BUTTON_COUNT] = {BUTTON_OTHER, BUTTON_UP, BUTTON_DOWN};
bool button_state[BUTTON_COUNT];


//  Display segment map
//    -      A
//  |   |  F   B
//    -      G
//  |   |  E   C
//    -      D

const uint8_t DISPLAY_AHOJ[] = {
    SEG_A | SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,   // A
    SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,           // H
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
    SEG_B | SEG_C | SEG_D | SEG_E                    // J
};

const uint8_t DISPLAY_ERROR = SEG_G;                               // -
const uint8_t DISPLAY_H = SEG_B | SEG_C | SEG_E | SEG_F | SEG_G;   // H
const uint8_t DISPLAY_S = SEG_A | SEG_C | SEG_D | SEG_F | SEG_G;   // S
const uint8_t DISPLAY_C = SEG_A | SEG_D | SEG_F | SEG_E;           // S

uint8_t brightness = BRIGHTNESS_MIN + 1;

uint8_t state = 1;
// state 0  ->  no display output
// state 1  ->  hot water temp / cold water temp
// state 2  ->  minimal hot water temperature
// state 3  ->  minimal cold water temperature

uint8_t temp_h_target = 30;
uint8_t temp_c_target = 20;

int8_t flash_counter = -1;

uint16_t pump_state_wait = 0;
uint8_t pump_last_state = LOW;

TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);



void setup() {
    analogReference(EXTERNAL);
    pinMode(SSR_PIN, OUTPUT);
    for (int i = 0; i < BUTTON_COUNT; i++) {
        pinMode(buttons[i], INPUT_PULLUP);
    }
    display.setBrightness(brightness);
    display.setSegments(DISPLAY_AHOJ);
    delay(1000);
    //EEPROM.write(EEPROM_TEMP_H, temp_h_target);
    //EEPROM.write(EEPROM_TEMP_C, temp_c_target);
    temp_h_target = EEPROM.read(EEPROM_TEMP_H);
    temp_c_target = EEPROM.read(EEPROM_TEMP_C);
}


// change global display state
void change_state() {
    state = state + 1;
    if (state > 3) {
        state = 0;
    }
}


// key up pressed
void handle_key_up() {
    if (state == 2 ) {
        start_flash();
        temp_h_target++;
    }
    if (state == 3 ) {
        start_flash();
        temp_c_target++;
    }
}


// key down pressed
void handle_key_down() {
    if (state == 2 ) {
        start_flash();
        temp_h_target--;
    }
    if (state == 3 ) {
        start_flash();
        temp_c_target--;
    }
}


// check and process keypress
void handle_input(){
    for (int i = 0; i < 7; i++) {
        for (int button = 0; button < BUTTON_COUNT; button++) {
            button_state[button] = digitalRead(buttons[button]);
        }
        bool something_happend = false;
        if (button_state[0] == 0) {
            change_state();
            something_happend = true;
        }
        if (button_state[1] == 0) {
            handle_key_up();
            something_happend = true;
        }
        if (button_state[2] == 0) {
            handle_key_down();
            something_happend = true;
        }
        if (something_happend) {
            break;
        }
        delay(100);
    }
}


// read temperature from pin
float get_term_from_pin(uint8_t pin_number) {
    float voltage;
    //Read voltage from pin
    voltage = analogRead(pin_number);

    // Convert voltage by thermistor resistance
    voltage = 1023 / voltage - 1;
    voltage = RESISTANCE / voltage;
  
    float temp;
    temp = voltage / TERM_NOM;          // (R/Ro)
    temp = log(temp);                   // ln(R/Ro)
    temp /= BETA_FACTOR;                // 1/B * ln(R/Ro)
    temp += 1.0 / (REF_TEMP + 273.15);  // + (1/To)
    temp = 1.0 / temp;                  // 1 / temp
    temp -= 273.15;                     // Convert Kelvin to Celsius

    return temp;
}


// start or stop ssr relay
void set_ssr_state(float temp_h, float temp_c) {
    uint8_t new_state = LOW;
    if ((temp_h >= temp_h_target) and (temp_c < temp_c_target))
    {
        new_state = HIGH;
    }
    if ((new_state != pump_last_state) and (pump_state_wait == 0)) {
        digitalWrite(SSR_PIN, new_state);
        pump_state_wait = PUMP_STATE_WAIT_MAX;
        pump_last_state = new_state;
    }
}


// set display when state = 1
void set_display_state_1(uint8_t display_out[], float temp_h, float temp_c) {
    if (temp_h < 100 and temp_h >= 0) {
        display_out[0] = display.encodeDigit(int(temp_h) / 10 % 10);
        display_out[1] = display.encodeDigit(int(temp_h) % 10);
    }
    else {
        display_out[0] = DISPLAY_ERROR;
        display_out[1] = DISPLAY_ERROR;
    }

    if (temp_c < 100 and temp_c >= 0) {
        display_out[2] = display.encodeDigit(int(temp_c) / 10 % 10);
        display_out[3] = display.encodeDigit(int(temp_c) % 10);
    }
    else {
        display_out[2] = DISPLAY_ERROR;
        display_out[3] = DISPLAY_ERROR;
    }
}


// set display when state = 2
void set_display_state_2(uint8_t display_out[]) {
    display_out[0] = DISPLAY_H;
    display_out[1] = DISPLAY_C;
    display_out[2] = display.encodeDigit(temp_h_target / 10 % 10);
    display_out[3] = display.encodeDigit(temp_h_target % 10);
}


// set display when state = 3
void set_display_state_3(uint8_t display_out[]) {
    display_out[0] = DISPLAY_S;
    display_out[1] = DISPLAY_C;
    display_out[2] = display.encodeDigit(temp_c_target / 10 % 10);
    display_out[3] = display.encodeDigit(temp_c_target % 10);
}


// set display dependent on global state
void  set_display_state(uint8_t display_out[], float temp_h, float temp_c) {
    if (state == 1 ) {
        set_display_state_1(display_out, temp_h, temp_c);
    }
    if (state == 2 ) {
        set_display_state_2(display_out);
    }
    if (state == 3 ) {
        set_display_state_3(display_out);
    }
}


// save temp to EEPROM
void save_eeprom() {
    EEPROM.update(EEPROM_TEMP_H, temp_h_target);
    EEPROM.update(EEPROM_TEMP_C, temp_c_target);
}


// flash with display
void flash(uint8_t display_out[]) {
    save_eeprom();
    display.setBrightness(BRIGHTNESS_MIN);
    display.setSegments(display_out);
    delay(250);
    display.setBrightness(BRIGHTNESS_MAX);
    display.setSegments(display_out);
    delay(250);
    display.setBrightness(brightness);
}


// flash with display after flash_counter reach zero
void start_flash() {
    flash_counter = FLASH_MAX;
}


// used to wait before save to protect EEPROM before to many writes
void check_flash_counter(uint8_t display_out[]) {
    if (flash_counter == 0) {
        flash_counter = -1;
        flash(display_out);
    }
    if (flash_counter > 0) {
        flash_counter--;
    }
}

void check_pump_counter()
{
    if (pump_state_wait > 0){
        pump_state_wait--;
    }
}

void loop() {
    uint8_t display_out[] = { 0, 0, 0, 0 };

    float temp_h = get_term_from_pin(TERM_PIN_H);
    float temp_c = get_term_from_pin(TERM_PIN_C);

    handle_input();
    set_display_state(display_out, temp_h, temp_c);
    check_pump_counter();
    check_flash_counter(display_out);

    //redraw display two times to prevents artifacts
    display.setSegments(display_out);
    display.setSegments(display_out);

    set_ssr_state(temp_h, temp_c);
  
    delay(300);
}
