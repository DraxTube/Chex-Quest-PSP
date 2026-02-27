# Chex Quest PSP Makefile

TARGET = chexquest

# PSP Assets - automatically detected
ICON0_FILE := $(wildcard ICON0.PNG)
PIC0_FILE  := $(wildcard PIC0.PNG)

ifneq ($(ICON0_FILE),)
PSP_EBOOT_ICON = ICON0.PNG
endif

ifneq ($(PIC0_FILE),)
PSP_EBOOT_PIC0 = PIC0.PNG
endif

OBJS = am_map.o \
       d_event.o \
       d_items.o \
       d_iwad.o \
       d_loop.o \
       d_main.o \
       d_mode.o \
       d_net.o \
       doomdef.o \
       doomgeneric.o \
       doomstat.o \
       dstrings.o \
       dummy.o \
       f_finale.o \
       f_wipe.o \
       g_game.o \
       hu_lib.o \
       hu_stuff.o \
       i_system.o \
       i_timer.o \
       i_video.o \
       info.o \
       m_argv.o \
       m_bbox.o \
       m_cheat.o \
       m_config.o \
       m_controls.o \
       m_fixed.o \
       m_menu.o \
       m_misc.o \
       m_random.o \
       memio.o \
       mus2mid.o \
       p_ceilng.o \
       p_doors.o \
       p_enemy.o \
       p_floor.o \
       p_inter.o \
       p_lights.o \
       p_map.o \
       p_maputl.o \
       p_mobj.o \
       p_plats.o \
       p_pspr.o \
       p_saveg.o \
       p_setup.o \
       p_sight.o \
       p_spec.o \
       p_switch.o \
       p_telept.o \
       p_tick.o \
       p_user.o \
       r_bsp.o \
       r_data.o \
       r_draw.o \
       r_main.o \
       r_plane.o \
       r_segs.o \
       r_sky.o \
       r_things.o \
       s_sound.o \
       sha1.o \
       sounds.o \
       st_lib.o \
       st_stuff.o \
       tables.o \
       v_video.o \
       w_checksum.o \
       w_file.o \
       w_file_stdc.o \
       w_main.o \
       w_wad.o \
       wi_stuff.o \
       z_zone.o \
       doomgeneric_psp.o \
       psp_sound.o

INCDIR = . $(PSPDEV)/psp/include $(PSPDEV)/psp/sdk/include
CFLAGS = -std=gnu99 -O2 -G0 -Wall \
         -DPSP -DNORMALUNIX -DFEATURE_SOUND=1 \
         -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
         -Wno-unused-variable -Wno-unused-parameter \
         -Wno-pointer-sign -Wno-missing-braces \
         -Wno-implicit-function-declaration -Wno-char-subscripts \
         -Wno-sign-compare -Wno-format -Wno-strict-aliasing \
         -Wno-old-style-definition -Wno-enum-conversion \
         -Wno-int-conversion

LIBDIR = . $(PSPDEV)/psp/lib $(PSPDEV)/psp/sdk/lib
LIBS = -lpspgu -lpspdisplay -lpspge -lpspctrl -lpsppower \
       -lpsprtc -lpspaudio -lm -lpspdebug -lpspdisplay \
       -lpspge -lpspctrl -lpspnet -lpspnet_apctl

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Chex Quest PSP
PSP_FW_VERSION = 660

BUILD_PRX = 1
PSP_LARGE_MEMORY = 1

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
