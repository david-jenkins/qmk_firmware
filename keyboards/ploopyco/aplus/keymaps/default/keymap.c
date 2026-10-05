/* Copyright 2023 Colin Lam (Ploopy Corporation)
 * Copyright 2020 Christopher Courtney, aka Drashna Jael're  (@drashna) <drashna@live.com>
 * Copyright 2019 Sunjun Kim
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H

#include "rgblight.h"
#include "printf.h"

#include "../../../common/tmag5273wheel.h"
#include "pointing_device_gestures.h"

/* State variables for wheels. */
uint16_t leftwheel_deadzone_center = 0;
uint16_t rightwheel_deadzone_center = 0;

uint16_t leftwheel_current_position = 0;
uint16_t rightwheel_current_position = 0;
uint32_t last_scroll_time = 0;

#define PLOOPY_DRAGSCROLL_HOLD_THRESHOLD_MS 200
static uint16_t drag_scroll_timer = 0;

/* Layer lighting, used to blink when making gestures. */
const rgblight_segment_t PROGMEM righty_nav_layer_colour[] =        RGBLIGHT_LAYER_SEGMENTS( {0, 2, HSV_NAVBLUE} );
const rgblight_segment_t PROGMEM lefty_nav_layer_colour[] =         RGBLIGHT_LAYER_SEGMENTS( {0, 2, HSV_NAVGREEN} );
const rgblight_segment_t PROGMEM control_layer_colour[] =           RGBLIGHT_LAYER_SEGMENTS( {0, 2, HSV_RED} );
const rgblight_segment_t PROGMEM gesture_layer_colour[] =           RGBLIGHT_LAYER_SEGMENTS( {0, 2, HSV_GESTUREYELLOW} );
const rgblight_segment_t PROGMEM option_changed_layer_colour[] =    RGBLIGHT_LAYER_SEGMENTS( {0, 2, HSV_OPTIONCHANGED} );

/* Lighting layer definitions. Later layers take precedence, so these
   are defined in the order that they will be displayed in. */
const rgblight_segment_t* const PROGMEM my_rgb_layers[] = RGBLIGHT_LAYERS_LIST(
    righty_nav_layer_colour,
    lefty_nav_layer_colour,
    control_layer_colour,
    gesture_layer_colour,
    option_changed_layer_colour
);

/* Colour layer names. */
enum {
    RIGHTY_NAV_LAYER_COLOUR = 0,
    LEFTY_NAV_LAYER_COLOUR,
    CONTROL_LAYER_COLOUR,
    GESTURE_LAYER_COLOUR,
    OPTION_CHANGED_LAYER_COLOUR    
};

 /* EEPROM memory for the various persistent states that the A+ can have. 
    See eeconfig_init_user() for default values and meanings. */
typedef union {
  uint32_t raw;
  struct {
    bool    left_handed :1;
    bool    horizontal_scroll_arrows :1;
    bool    vertical_scroll_arrows :1;
    bool    drag_scroll_mode :1;
    uint8_t led_brightness: 8;
  };
} user_config_t;

user_config_t user_config;

/* Add custom keycodes for control layer. */
enum my_keycodes {
  PKC_TGL_MIRROR = SAFE_RANGE,
  PKC_TGL_VERT_SCRL,
  PKC_TGL_HORIZ_SCRL,
  PKC_TGL_DRAG_SCRL,
  PKC_GESTURE,
  PKC_DRAG_SCROLL,
  PKC_DRAG_SCROLL_TAP_M4,
  PKC_DRAG_SCROLL_TAP_M5,
  PKC_ADJUST_LED_BRIGHTNESS,
  PKC_BLINKY_DPI_CONFIG
};

/* Layer names. */
enum {
    LAYER_NAV_RIGHT_HANDED = 0,
    LAYER_NAV_LEFT_HANDED,
    LAYER_CONTROL
};

/* Override default function in submodule code so that we can blink whenever
   a successful gesture is registered. */
void pointing_device_gestures_trigger(uint8_t direction) {
    
    /* Directions are numbered off 0-7, starting with east. */
    switch( direction ) {
        case 0: /* East, Next Virtual Desktop. */
            /* On Windows, send CTRL + LGUI + RIGHT. */
            if ( detected_host_os() == OS_WINDOWS ) {
                tap_code16( LCG( KC_RIGHT ) );
            }
            /* On Linux, send CTRL + ALT + RIGHT. */
            else if( detected_host_os() == OS_LINUX ) {
                tap_code16( LCA( KC_RIGHT ) );
            }
            /* On MacOs, send Control + RIGHT. */
            else {
                tap_code16( LCTL( KC_RIGHT ) );
            }
            break;
        case 1: /* South-East, Paste. */
            tap_code16( C(KC_V) );
            break;
        case 2: /* South, Copy. */
            tap_code16( C(KC_C) );
            break;
        case 3: /* South-West, Cut. */
            tap_code16( C(KC_X) );
            break;
        case 4: /* West, Previous Virtual Desktop. */
            /* On Windows, send CTRL + LGUI + LEFT. */
            if ( detected_host_os() == OS_WINDOWS ) {
                tap_code16( LCG( KC_LEFT ) );
            }
            /* On Linux, send CTRL + ALT + LEFT. */
            else if( detected_host_os() == OS_LINUX ) {
                tap_code16( LCA( KC_LEFT ) );
            }
            /* On MacOs, send Control + LEFT. */
            else {
                tap_code16( LCTL( KC_LEFT ) );
            }
            break;
        case 5: /* North-West, Undo. */
            tap_code16( C(KC_Z) );
            break;
        case 6: /* North, Play/Pause Audio. */
            tap_code16( KC_MEDIA_PLAY_PAUSE );
            break;
        case 7: /* North-East, Redo. */
            /* On Windows/Linux, send CTRL + Y. */
            if ( detected_host_os() == OS_WINDOWS || detected_host_os() == OS_LINUX ) {
                tap_code16( C(KC_Y) );
            }
            /* On MacOs, send CMD + SHIFT + Z. */
            else {
                register_code(KC_LCMD);
                register_code(KC_LSFT);
                tap_code(KC_Z);
                unregister_code(KC_LCMD);
                unregister_code(KC_LSFT);
            }
            break;
        default:
            break;
    }

    /* Flash light to indicate successful gesture processing. */
    rgblight_blink_layer(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT*2);
}

/* Mouse gestures keymap. Since we cannot use custom keycodes in this array and
   we've overridden the function that uses it, we define this here with
   KC_NO (no action) to prevent a compiler error. See pointing_device_gestures_trigger()
   for what the gestures actually do. */
const uint16_t PROGMEM pointing_device_gestures[NUM_GESTURE_DIRECTIONS] =
    GESTURES_CARDINAL_AND_ORDINAL_DIRECTIONS( KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO );

/* Keymap. */
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    // Base layer with all of the mouse-related stuff for everyday use.
    [LAYER_NAV_RIGHT_HANDED] = LAYOUT(  PKC_DRAG_SCROLL_TAP_M4, PKC_DRAG_SCROLL_TAP_M5, PKC_DRAG_SCROLL, MS_BTN2, 
                                        MS_BTN1, MS_BTN3, 
                                        PKC_GESTURE, TG(LAYER_CONTROL) ),
    // Mirror-image of right-handed layout for lefties.
    [LAYER_NAV_LEFT_HANDED] = LAYOUT(   MS_BTN2, PKC_DRAG_SCROLL, PKC_DRAG_SCROLL_TAP_M4, PKC_DRAG_SCROLL_TAP_M5, 
                                        MS_BTN3, MS_BTN1, 
                                        TG(LAYER_CONTROL), PKC_GESTURE ),
    // Layer for all of the customization options.
    [LAYER_CONTROL] = LAYOUT(           PKC_BLINKY_DPI_CONFIG, PKC_ADJUST_LED_BRIGHTNESS, PKC_TGL_VERT_SCRL, PKC_TGL_HORIZ_SCRL,
                                        PKC_TGL_MIRROR, PKC_TGL_DRAG_SCRL, 
                                        TG(LAYER_CONTROL), TG(LAYER_CONTROL) ),
    [3] = LAYOUT( _______, _______, _______, _______, _______, _______, _______, _______ ),
    [4] = LAYOUT( _______, _______, _______, _______, _______, _______, _______, _______ ),
    [5] = LAYOUT( _______, _______, _______, _______, _______, _______, _______, _______ ),
    [6] = LAYOUT( _______, _______, _______, _______, _______, _______, _______, _______ ),
    [7] = LAYOUT( _______, _______, _______, _______, _______, _______, _______, _______ )
};

/* Called whenever layers are modified. */
layer_state_t layer_state_set_user(layer_state_t state) {
    /* Set layer colours. */
    switch (get_highest_layer(state)) {
        case LAYER_NAV_RIGHT_HANDED:
            rgblight_set_layer_state(RIGHTY_NAV_LAYER_COLOUR, true);
            
            rgblight_set_layer_state(LEFTY_NAV_LAYER_COLOUR, false);
            rgblight_set_layer_state(CONTROL_LAYER_COLOUR, false);            

            dprintf("change layer: right-handed nav\n");
            break;
        case LAYER_NAV_LEFT_HANDED:
            rgblight_set_layer_state(LEFTY_NAV_LAYER_COLOUR, true);

            rgblight_set_layer_state(RIGHTY_NAV_LAYER_COLOUR, false);
            rgblight_set_layer_state(CONTROL_LAYER_COLOUR, false);  

            dprintf("change layer: left-handed nav\n");
            break;
        case LAYER_CONTROL:
            rgblight_set_layer_state(CONTROL_LAYER_COLOUR, true);
            
            rgblight_set_layer_state(RIGHTY_NAV_LAYER_COLOUR, false);
            rgblight_set_layer_state(LEFTY_NAV_LAYER_COLOUR, false); 

            dprintf("change layer: control\n");
            break;
        default:
            rgblight_setrgb(RGB_CYAN);
            dprintf("change layer: oopsie, invalid layer!\n");
            break;
    }
    return state;
}

/* Set sane defaults when EEPROM is reset. */
void eeconfig_init_user(void) {

    user_config.raw = 0;

    user_config.left_handed = false;                // Right-handed.
    user_config.horizontal_scroll_arrows = false;   // False = Scroll events, True = left/right arrows.
    user_config.vertical_scroll_arrows = false;     // False = Scroll events, True = up/down arrows.
    user_config.drag_scroll_mode = false;           // Hold to activate.
    user_config.led_brightness = 4;                 // Brightest mode by default.

    eeconfig_update_user(user_config.raw);
}

void keyboard_pre_init_user(void) {

    /* Set up GPIO for both TMAG sensors. */
    gpio_set_pin_output_push_pull(TMAG5273_D0_PWR_PIN);
    gpio_set_pin_output_push_pull(TMAG5273_D1_PWR_PIN);
    gpio_write_pin_low(TMAG5273_D0_PWR_PIN);
    gpio_write_pin_low(TMAG5273_D1_PWR_PIN);

}

void keyboard_post_init_user(void) {
    /* ============================ Debug Setup ============================ */

    /* If you turn these on, also uncomment CONSOLE_ENABLE = yes in
       post_rules.mk! */
    //debug_enable = true;
    //debug_matrix=true;
    //debug_keyboard=true;
    //debug_mouse=true;

    /* ========================== Scroll Wheel Setup ======================= */

    /* Start up the two wheels. */
    tmag5273_init();

    /* Init TMAG5273 D0 (left wheel) */
    gpio_write_pin_high(TMAG5273_D0_PWR_PIN);
    wait_us(370);  // TMAG tstart_power_up, plus 100us margin
    tmag5273_init_device(TMAG5273_D0_I2C_ADDRESS);
    wait_ms(20);
    leftwheel_current_position = tmag5273_get_angle(TMAG5273_D0_I2C_ADDRESS);
    leftwheel_deadzone_center = leftwheel_current_position;


    /* Init TMAG5273 D1 (right wheel) */
    gpio_write_pin_high(TMAG5273_D1_PWR_PIN);
    wait_us(370);  // TMAG tstart_power_up, plus 100us margin
    tmag5273_init_device(TMAG5273_D1_I2C_ADDRESS);
    wait_ms(20);
    rightwheel_current_position = tmag5273_get_angle(TMAG5273_D1_I2C_ADDRESS);
    rightwheel_deadzone_center = rightwheel_current_position;


    /* =================== Persistent Configuration Setup ================== */

    /* Load up configuration details from the EEPROM. */
    user_config.raw = eeconfig_read_user();

    /* Set the startup layer based on handed-ness. */
    if( user_config.left_handed ) {
        layer_on(LAYER_NAV_LEFT_HANDED);
    }


    /* ============================ Lighting Setup ========================= */

    /* Enable the LED layers. */
    rgblight_layers = my_rgb_layers;

    /* Enable RGB lighting for layer indicators. */
    rgblight_enable_noeeprom();
    rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);

    /* Set an initial colour to establish a baseline brightness for the RGB layers. */
    rgblight_sethsv(0, 0, RGBLIGHT_VAL_STEP * user_config.led_brightness);
}  

void _drag_scroll_tap(keyrecord_t* record, enum qk_keycode_defines keycode)
{
    is_drag_scroll = record->event.pressed;
    /* On press start timer */
    if (record->event.pressed) {
        drag_scroll_timer = timer_read();
    }
    /* If released before PLOOPY_DRAGSCROLL_HOLD_THRESHOLD_MS timeout then fire MS_BTN4 */
    else if (timer_elapsed(drag_scroll_timer) < PLOOPY_DRAGSCROLL_HOLD_THRESHOLD_MS) {
        tap_code16(keycode);
    }
}

bool process_record_user(uint16_t keycode, keyrecord_t* record) {
    switch (keycode) {
        case PKC_TGL_MIRROR:
            if (record->event.pressed) {
                /* Toggle handed-ness, record new state to EEPROM. */
                user_config.left_handed ^= 1;
                eeconfig_update_user(user_config.raw);

                /* Activate the correct navigation layer and blink the LEDs. */
                if( user_config.left_handed ) {
                    dprintf( "Lefty nav mode.\n");
                    layer_on(LAYER_NAV_LEFT_HANDED);
                    rgblight_blink_layer(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT*2);
                }
                else {
                    dprintf( "Righty nav mode.\n");
                    layer_off(LAYER_NAV_LEFT_HANDED);
                    rgblight_blink_layer_repeat(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT, 2);
                }
            }
            return true;
        case PKC_TGL_VERT_SCRL:
            if (record->event.pressed) {
                user_config.vertical_scroll_arrows ^= 1;
                eeconfig_update_user(user_config.raw);

                if( user_config.vertical_scroll_arrows ) {
                    rgblight_blink_layer(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT*2);
                    dprintf("Vert. scroll = arrows.\n");
                }
                else {
                    rgblight_blink_layer_repeat(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT, 2);
                    dprintf("Vert. scroll = scroll events.\n");
                }
            }
            return true;
        case PKC_TGL_HORIZ_SCRL:
            if (record->event.pressed) {
                user_config.horizontal_scroll_arrows ^= 1;
                eeconfig_update_user(user_config.raw);

                if( user_config.horizontal_scroll_arrows ) {
                    rgblight_blink_layer(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT*2);
                    dprintf("Horiz. scroll = arrows.\n");
                }
                else {
                    rgblight_blink_layer_repeat(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT, 2);
                    dprintf("Horiz. scroll = scroll events.\n");
                }
            }
            return true;
        case PKC_TGL_DRAG_SCRL:
            if (record->event.pressed) {
                user_config.drag_scroll_mode ^= 1;
                eeconfig_update_user(user_config.raw);

                if( user_config.drag_scroll_mode ) {
                    rgblight_blink_layer(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT*2);
                    dprintf("Drag scroll: toggle to activate.\n");
                }
                else {
                    rgblight_blink_layer_repeat(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT, 2);
                    dprintf("Drag scroll: hold to activate.\n");
                }
            }
            return true;
        case PKC_GESTURE:
            if (record->event.pressed) {
                /* Send correct gesture mode activation based on preference. */
                pointing_device_gestures_start();
                rgblight_set_layer_state(GESTURE_LAYER_COLOUR, true);
            }
            /* Switch is released. */
            else {
                pointing_device_gestures_end();
                rgblight_set_layer_state(GESTURE_LAYER_COLOUR, false);
            }
            return true; 
        case PKC_DRAG_SCROLL:
            if( user_config.drag_scroll_mode ) {
                if (record->event.pressed) {
                    toggle_drag_scroll();
                }
            }
            /* Press and hold mode. */
            else {
                is_drag_scroll = record->event.pressed;
            }
            return true;
        case PKC_DRAG_SCROLL_TAP_M4:
            _drag_scroll_tap(record, MS_BTN4);
            return true;
        case PKC_DRAG_SCROLL_TAP_M5:
            _drag_scroll_tap(record, MS_BTN5);
            return true;
        case PKC_ADJUST_LED_BRIGHTNESS:
            /* Increase brightness in RGBLIGHT_VAL_STEP steps. 
               Go to minimum brightness if the max brightness is hit. */
            if (record->event.pressed) {
                if( rgblight_get_val() >= RGBLIGHT_VAL_STEP * 4 ) {
                    rgblight_decrease_val_noeeprom();
                    rgblight_decrease_val_noeeprom();
                    rgblight_decrease_val_noeeprom();
                    user_config.led_brightness = 1;
                    eeconfig_update_user(user_config.raw);
                }
                else {
                    rgblight_increase_val_noeeprom();
                    user_config.led_brightness += 1;
                    eeconfig_update_user(user_config.raw);
                }

                dprintf("Adjusted LED brightness. Current brightness = %d/255\n", rgblight_get_val());
            }
            return true;
        case PKC_BLINKY_DPI_CONFIG:
            if (record->event.pressed) {
                cycle_dpi();

                dprintf("DPI changed. Current DPI = %d\n", dpi_array[keyboard_config.dpi_config]);
                rgblight_blink_layer_repeat(OPTION_CHANGED_LAYER_COLOUR, OPTION_CHANGE_BLINK_TIMEOUT, keyboard_config.dpi_config + 1);
            }
            return true;         
        default:
            return true;
    }
}

/* State variables for the scroll events. */
float leftwheel_accumulated = 0;
float rightwheel_accumulated = 0;

int16_t horizontal_arrow_scroll_tick = 0;
int16_t vertical_arrow_scroll_tick = 0;

int16_t volume_scroll_tick = 0;

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    int16_t leftwheel_delta = tmag5273_get_delta(TMAG5273_D0_I2C_ADDRESS, &leftwheel_deadzone_center);
    int16_t rightwheel_delta = tmag5273_get_delta(TMAG5273_D1_I2C_ADDRESS, &rightwheel_deadzone_center);

    /* Ignore wheel events if the knobs are depressed */
    
    /* Check left knob. */
    if( matrix_is_on(0,6) ) {
        leftwheel_delta = 0;
    }

    /* Check right knob. */
    if( matrix_is_on(0,7) ) {
        rightwheel_delta = 0;
    }

    /* If both deltas come back zero, return early; no discernable events detected */
    if (leftwheel_delta == 0 && rightwheel_delta == 0) {
        return mouse_report;
    }

    /* If we're on the control layers, both wheels adjust the volume. */
    if( layer_state_is(LAYER_CONTROL) ) {

        volume_scroll_tick += leftwheel_delta;
        volume_scroll_tick += rightwheel_delta;

        if( volume_scroll_tick > TMAG5273_VOLUME_SCROLL_TICK_SIZE ) {
            tap_code(KC_VOLU);
            volume_scroll_tick = 0;
        }
        else if( volume_scroll_tick < -TMAG5273_VOLUME_SCROLL_TICK_SIZE ) {
            tap_code(KC_VOLD);
            volume_scroll_tick = 0;
        }

        /* Return early -- no other scrolling activity happens in the
            control layer. */
        return mouse_report;
    }

    /* If scroll arrow mode is activated for horizontal scrolling, then we do that. 
        This behaviour is the same across OSes. */
    if( user_config.horizontal_scroll_arrows ) {

        if( user_config.left_handed ) {
            horizontal_arrow_scroll_tick += leftwheel_delta;
            /* Set the delta to zero so we don't scroll *and* arrow at the same time. */
            leftwheel_delta = 0;
        }
        else {
            horizontal_arrow_scroll_tick += rightwheel_delta;
            /* Set the delta to zero so we don't scroll *and* arrow at the same time. */
            rightwheel_delta = 0;
        }

        if( horizontal_arrow_scroll_tick > TMAG5273_HORIZ_SCROLL_TICK_SIZE ) {
            tap_code(KC_RIGHT);
            horizontal_arrow_scroll_tick = 0;
        }
        else if( horizontal_arrow_scroll_tick < -TMAG5273_HORIZ_SCROLL_TICK_SIZE ) {
            tap_code(KC_LEFT);
            horizontal_arrow_scroll_tick = 0;
        }

    }

    /* If scroll arrow mode is activated for vertical scrolling, then we do that. 
        This behaviour is the same across OSes. */
    if( user_config.vertical_scroll_arrows ) {

        if( user_config.left_handed ) {
            vertical_arrow_scroll_tick += rightwheel_delta;
            /* Set the delta to zero so we don't scroll *and* arrow at the same time. */
            rightwheel_delta = 0;
        }
        else {
            vertical_arrow_scroll_tick += leftwheel_delta;
            /* Set the delta to zero so we don't scroll *and* arrow at the same time. */
            leftwheel_delta = 0;
        }
        if( vertical_arrow_scroll_tick > TMAG5273_VERT_SCROLL_TICK_SIZE ) {
            tap_code(KC_DOWN);
            vertical_arrow_scroll_tick = 0;
        }
        else if( vertical_arrow_scroll_tick < -TMAG5273_VERT_SCROLL_TICK_SIZE ) {
            tap_code(KC_UP);
            vertical_arrow_scroll_tick = 0;
        }
    }

    /* Default scroll resolution, e.g. when high res scroll is disabled or it's enabled and we are on macOS */
    float resolution = 1;
#ifdef POINTING_DEVICE_HIRES_SCROLL_ENABLE
    /* If high res scroll is enabled AND we are on Winows or Linux, update the resolution to something suitable */
    if ( (detected_host_os() == OS_WINDOWS || detected_host_os() == OS_LINUX) )
        resolution = pointing_device_get_hires_scroll_resolution() * 2;
#endif

    /* If any delta is left after all that, trigger the scroll events */
    if (leftwheel_delta != 0) {
        leftwheel_accumulated += ((float)leftwheel_delta * resolution) / TMAG5273_VERTICAL_WHEEL_SPEED_DIV;
        if( user_config.left_handed )
            mouse_report.h = (int32_t)leftwheel_accumulated;
        else
            mouse_report.v = (int32_t)leftwheel_accumulated;
        leftwheel_accumulated -= (int32_t)leftwheel_accumulated;
    }

    if (rightwheel_delta != 0) {
        rightwheel_accumulated += ((float)rightwheel_delta * resolution) / TMAG5273_HORIZONAL_WHEEL_SPEED_DIV;
        if( user_config.left_handed )
            mouse_report.v = (int32_t)rightwheel_accumulated;
        else
            mouse_report.h = (int32_t)rightwheel_accumulated;
        rightwheel_accumulated -= (int32_t)rightwheel_accumulated;
    }

    return mouse_report;
}