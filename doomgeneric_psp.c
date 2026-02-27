/* ==================== Main ==================== */

int main(int argc, char **argv)
{
    /* 
     * On PSP, the working directory is NOT necessarily the game folder.
     * We must use the full path relative to the EBOOT location.
     * When launched from ms0:/PSP/GAME/ChexQuest/EBOOT.PBP,
     * the WAD should be at ms0:/PSP/GAME/ChexQuest/chex.wad
     *
     * We try multiple common paths.
     */
    static char wad_path[256];
    static char *default_argv[4];
    static int default_argc = 3;
    
    /* Try to find the WAD file */
    const char *search_paths[] = {
        "ms0:/PSP/GAME/ChexQuest/chex.wad",
        "ms0:/PSP/GAME/chexquest/chex.wad",
        "./chex.wad",
        "chex.wad",
        NULL
    };
    
    const char *found_path = "chex.wad"; /* fallback */
    int i;
    
    for (i = 0; search_paths[i] != NULL; i++)
    {
        FILE *test = fopen(search_paths[i], "rb");
        if (test != NULL)
        {
            fclose(test);
            found_path = search_paths[i];
            break;
        }
    }
    
    default_argv[0] = "chexquest";
    default_argv[1] = "-iwad";
    default_argv[2] = (char *)found_path;
    default_argv[3] = NULL;

    if (argc < 2)
    {
        doomgeneric_Create(default_argc, default_argv);
    }
    else
    {
        doomgeneric_Create(argc, argv);
    }

    while (running)
    {
        doomgeneric_Tick();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
