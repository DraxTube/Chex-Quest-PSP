TARGET = chexquest

OBJS = am_map.o d_event.o d_items.o d_iwad.o d_loop.o d_main.o d_mode.o \
       d_net.o doomdef.o doomgeneric.o doomstat.o dstrings.o dummy.o \
       f_finale.o f_wipe.o g_game.o hu_lib.o hu_stuff.o i_system.o \
       i_timer.o i_video.o info.o m_argv.o m_bbox.o m_cheat.o m_config.o \
       m_controls.o m_fixed.o m_menu.o m_misc.o m_random.o memio.o \
       mus2mid.o p_ceilng.o p_doors.o p_enemy.o p_floor.o p_inter.o \
       p_lights.o p_map.o p_maputl.o p_mobj.o p_plats.o p_pspr.o \
       p_saveg.o p_setup.o p_sight.o p_spec.o p_switch.o p_telept.o \
       p_tick.o p_user.o r_bsp.o r_data.o r_draw.o r_main.o r_plane.o \
       r_segs.o r_sky.o r_things.o s_sound.o sha1.o sounds.o st_lib.o \
       st_stuff.o tables.o v_video.o w_checksum.o w_file.o w_file_stdc.o \
       w_main.o w_wad.o wi_stuff.o z_zone.o doomgeneric_psp.o

INCDIR =
CFLAGS = -std=gnu99 -O2 -G0 -Wall -DPSP -DNORMALUNIX -DFEATURE_SOUND=0 \
         -DDOOMGENERIC_RESX=480 -DDOOMGENERIC_RESY=272 \
         -Wno-unused-variable -Wno-unused-parameter -Wno-pointer-sign \
         -Wno-missing-braces -Wno-implicit-function-declaration \
         -Wno-char-subscripts -Wno-sign-compare -Wno-format \
         -Wno-strict-aliasing -Wno-old-style-definition \
         -Wno-enum-conversion -Wno-int-conversion

LIBDIR =
LDFLAGS =

LIBS = -lpspgu -lpspdisplay -lpspge -lpspctrl -lpsppower -lpsprtc -lm

PSPSDK_LIBS =

BUILD_PRX = 1
PSP_FW_VERSION = 660

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Chex Quest PSP

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
