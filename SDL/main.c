#include <stdbool.h>
#include <stdio.h>
#include <signal.h>
#include <SDL/SDL.h>
#include <Core/gb.h>
#include "utils.h"
#include "gui.h"
#define AUDIO_FREQUENCY 11025

GB_gameboy_t gb;
static bool paused = false;
static bool underclock_down = false, rewind_down = false, do_rewind = false, rewind_paused = false, turbo_down = false;
static double clock_mutliplier = 1.0;

static char *filename = NULL;
static bool should_free_filename = false;
static char *battery_save_path_ptr;

//SDL_AudioDeviceID device_id;

static const GB_model_t sdl_to_internal_model[] =
{
    [MODEL_DMG] = GB_MODEL_DMG_B,
    [MODEL_CGB] = GB_MODEL_CGB_E,
    [MODEL_AGB] = GB_MODEL_AGB
};

void set_filename(const char *new_filename, bool new_should_free)
{
    if (filename && should_free_filename) {
        SDL_free(filename);
    }
    filename = (char *) new_filename;
    should_free_filename = new_should_free;
}

static SDL_AudioSpec want_aspec, have_aspec;


static void open_menu(void)
{
    bool audio_playing = SDL_GetAudioStatus() == SDL_AUDIO_PLAYING;
    if (audio_playing) {
        SDL_PauseAudio( 1);
    }
    run_gui(true);
    if (audio_playing) {
        SDL_PauseAudio( 0);
    }
}

static void handle_events(GB_gameboy_t *gb)
{
#ifdef __APPLE__
#define MODIFIER KMOD_GUI
#else
#define MODIFIER KMOD_CTRL
#endif
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type) {
            case SDL_QUIT:
                pending_command = GB_SDL_QUIT_COMMAND;
                break;

            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: {
						pending_command = GB_SDL_QUIT_COMMAND;
                       // open_menu();
                        break;
                    }

                    case SDLK_r:
                        /*if (event.key.keysym.mod & MODIFIER) {
                            pending_command = GB_SDL_RESET_COMMAND;
                        }*/
                        break;
                    
                    case SDLK_p:
                        if (event.key.keysym.mod & MODIFIER) {
                            paused = !paused;
                        }
                        break;
                        
                    case SDLK_m:
                        if (event.key.keysym.mod & MODIFIER) {
#ifdef __APPLE__
                            // Can't override CMD+M (Minimize) in SDL
                            if (!(event.key.keysym.mod & KMOD_SHIFT)) {
                                break;
                            }
#endif
							bool audio_playing = SDL_GetAudioStatus() == SDL_AUDIO_PLAYING;
                            if (audio_playing) {
                                SDL_PauseAudio(1);
                            }
                            else if (!audio_playing) {
                                SDL_PauseAudio(0);
                            }
                        }
                    break;
                    
                        
                    default:
                        /* Save states */
                        if (event.key.keysym.scancode >= SDLK_1 && event.key.keysym.scancode <= SDLK_0) {
                            if (event.key.keysym.mod & MODIFIER) {
                                command_parameter = (event.key.keysym.sym - SDLK_1 + 1) % 10;
                                
                                if (event.key.keysym.mod & KMOD_SHIFT) {
                                    pending_command = GB_SDL_LOAD_STATE_COMMAND;
                                }
                                else {
                                    pending_command = GB_SDL_SAVE_STATE_COMMAND;
                                }
                            }
                        }
                        break;
                }
            case SDL_KEYUP: // Fallthrough
                if (event.key.keysym.sym == configuration.keys[8]) {
                    turbo_down = event.type == SDL_KEYDOWN;
                    GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
                }
                else if (event.key.keysym.sym == configuration.keys_2[0]) {
                    rewind_down = event.type == SDL_KEYDOWN;
                    if (event.type == SDL_KEYUP) {
                        rewind_paused = false;
                    }
                    GB_set_turbo_mode(gb, turbo_down, turbo_down && rewind_down);
                }
                else if (event.key.keysym.sym == configuration.keys_2[1]) {
                    underclock_down = event.type == SDL_KEYDOWN;
                }
                else {
                    for (unsigned i = 0; i < GB_KEY_MAX; i++) {
                        if (event.key.keysym.sym == configuration.keys[i]) {
                            GB_set_key_state(gb, i, event.type == SDL_KEYDOWN);
                        }
                    }
                }
                break;
            default:
                break;
                }
        }
    }

static void vblank(GB_gameboy_t *gb)
{
    //render_texture(active_pixel_buffer, NULL);
	SDL_Flip(screen);
    handle_events(gb);
}


static uint16_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return SDL_MapRGB(screen->format, r, g, b);
}

static void audio_callback(void *gb, Uint8 *stream, int len)
{
    if (GB_is_inited(gb)) {
        GB_apu_copy_buffer(gb, (GB_sample_t *) stream, len / sizeof(GB_sample_t));
    }
    else {
        memset(stream, 0, len);
    }
}
    
static bool handle_pending_command(void)
{
    switch (pending_command) {
        case GB_SDL_LOAD_STATE_COMMAND:
        case GB_SDL_SAVE_STATE_COMMAND: {
            char save_path[strlen(filename) + 4];
            char save_extension[] = ".s0";
            save_extension[2] += command_parameter;
            replace_extension(filename, strlen(filename), save_path, save_extension);
            
            if (pending_command == GB_SDL_LOAD_STATE_COMMAND) {
                GB_load_state(&gb, save_path);
            }
            else {
                GB_save_state(&gb, save_path);
            }
            return false;
        }
            
        case GB_SDL_RESET_COMMAND:
            GB_save_battery(&gb, battery_save_path_ptr);
            return true;
            
        case GB_SDL_NO_COMMAND:
            return false;
            
        case GB_SDL_NEW_FILE_COMMAND:
            return true;
            
        case GB_SDL_QUIT_COMMAND:
            GB_save_battery(&gb, battery_save_path_ptr);
            //exit(0);
    }
    return false;
}

static void run(void)
{
    pending_command = GB_SDL_NO_COMMAND;
restart:
    if (GB_is_inited(&gb)) {
        GB_switch_model_and_reset(&gb, sdl_to_internal_model[configuration.model]);
    }
    else {
        GB_init(&gb, sdl_to_internal_model[configuration.model]);
        GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
        GB_set_pixels_output(&gb, screen->pixels);
        GB_set_rgb_encode_callback(&gb, rgb_encode);
        GB_set_sample_rate(&gb, have_aspec.freq);
    }
    
    bool error = false;
    const char * const boot_roms[] = {"dmg_boot.bin", "cgb_boot.bin", "agb_boot.bin"};
    error = GB_load_boot_rom(&gb, executable_relative_path(boot_roms[configuration.model]));
    
    error = GB_load_rom(&gb, filename);
    
    size_t path_length = strlen(filename);
    
    /* Configure battery */
    char battery_save_path[path_length + 5]; /* At the worst case, size is strlen(path) + 4 bytes for .sav + NULL */
    replace_extension(filename, path_length, battery_save_path, ".sav");
    battery_save_path_ptr = battery_save_path;
    GB_load_battery(&gb, battery_save_path);
    
    char symbols_path[path_length + 5];
    replace_extension(filename, path_length, symbols_path, ".sym");
    
    /* Run emulation */
    while (true) {
        GB_run(&gb);
        
        if (pending_command == GB_SDL_QUIT_COMMAND)
        {
			break;
		}
        
        /* These commands can't run in the handle_event function, because they're not safe in a vblank context. */
        if (handle_pending_command()) {
            pending_command = GB_SDL_NO_COMMAND;
            goto restart;
        }
        pending_command = GB_SDL_NO_COMMAND;
    }
}

int main(int argc, char **argv)
{
#define str(x) #x
#define xstr(x) str(x)
    fprintf(stderr, "SameBoy v" xstr(VERSION) "\n");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s [rom]\n", argv[0]);
		return 1;
    }
    
	filename = argv[1];

    SDL_Init(SDL_INIT_EVERYTHING);
    
    screen = SDL_SetVideoMode(160, 144, 16, SDL_HWSURFACE);

    /* Configure Audio */
    memset(&want_aspec, 0, sizeof(want_aspec));
    want_aspec.freq = AUDIO_FREQUENCY;
    want_aspec.format = AUDIO_S16SYS;
    want_aspec.channels = 2;
    want_aspec.samples = 512;
    want_aspec.callback = audio_callback;
    want_aspec.userdata = &gb;
    SDL_OpenAudio(&want_aspec, &have_aspec);

    if (configuration.model >= MODEL_MAX) {
        configuration.model = MODEL_CGB;
    }
    
    SDL_PauseAudio(0);
    
    run();
    
	SDL_PauseAudio(1);
	
    if (screen) SDL_FreeSurface(screen);
    SDL_Quit();
    
    return 0;
}
