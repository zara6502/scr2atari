/*
 * scr2atari.c
 *
 * ZX Spectrum .SCR -> Atari XL/XE XEX / BIN / PNG / QOI converter.
 *
 * Supports:
 *   -color      create 320x240 Atari color reference PNG/QOI
 *   -colorn     create 320x240 Atari color reference using nearest colors
 *   -colorn       nearest GTIA colors
 *   -colorn-bres  nearest GTIA colors + Bresenham resize
 *   -colorn-area  nearest GTIA colors + area resize
 *   -xex FILE   create Atari XEX
 *   -bin FILE   create raw Atari screen BIN
 *   -png FILE   create RGB PNG
 *   -qoi FILE   create RGB QOI
 *   -o FILE     output file, format determined by extension
 *   -d          Floyd-Steinberg dithering
 *   -dw         dithering with ZX white preservation
 *   -do         ordered diagonal dithering
 *   -j N        number of worker threads
 *   -q          quiet mode
 *   -h, --help  show help
 *
 * Multiple input .SCR files and wildcard masks * and ? are supported.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "filters.h"
#include "files.h"
#include "xex.h"
#include "scr.h"
#include "color.h"
#include "png.h"
#include "qoi.h"
#include "atari.h"

#ifdef _WIN32

#include <windows.h>
#include <direct.h>
#include <io.h>

#else

#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#endif


/* ========================================================= */
/* Platform thread abstraction                               */
/* ========================================================= */

#ifdef _WIN32

typedef HANDLE Thread;
typedef CRITICAL_SECTION Mutex;
typedef CONDITION_VARIABLE Cond;

static void mutex_init(Mutex *m)
{
    InitializeCriticalSection(m);
}

static void mutex_destroy(Mutex *m)
{
    DeleteCriticalSection(m);
}

static void mutex_lock(Mutex *m)
{
    EnterCriticalSection(m);
}

static void mutex_unlock(Mutex *m)
{
    LeaveCriticalSection(m);
}

static void cond_init(Cond *c)
{
    InitializeConditionVariable(c);
}

static void cond_destroy(Cond *c)
{
    (void)c;
}

static void cond_wait(Cond *c, Mutex *m)
{
    SleepConditionVariableCS(c, m, INFINITE);
}

static void cond_signal(Cond *c)
{
    WakeConditionVariable(c);
}

static void cond_broadcast(Cond *c)
{
    WakeAllConditionVariable(c);
}

#else

typedef pthread_t Thread;
typedef pthread_mutex_t Mutex;
typedef pthread_cond_t Cond;

static void mutex_init(Mutex *m)
{
    pthread_mutex_init(m, NULL);
}

static void mutex_destroy(Mutex *m)
{
    pthread_mutex_destroy(m);
}

static void mutex_lock(Mutex *m)
{
    pthread_mutex_lock(m);
}

static void mutex_unlock(Mutex *m)
{
    pthread_mutex_unlock(m);
}

static void cond_init(Cond *c)
{
    pthread_cond_init(c, NULL);
}

static void cond_destroy(Cond *c)
{
    pthread_cond_destroy(c);
}

static void cond_wait(Cond *c, Mutex *m)
{
    pthread_cond_wait(c, m);
}

static void cond_signal(Cond *c)
{
    pthread_cond_signal(c);
}

static void cond_broadcast(Cond *c)
{
    pthread_cond_broadcast(c);
}

#endif


static void print_error(Mutex *mutex,
                        const char *format,
                        ...)
{
    va_list args;

    if (mutex)
        mutex_lock(mutex);

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    if (mutex)
        mutex_unlock(mutex);
}


/* ========================================================= */
/* Atari BIN                                                 */
/* ========================================================= */

static int write_bin(const char *filename,
                     const uint8_t *screen)
{
    FILE *f;

    f = fopen(filename, "wb");

    if (!f)
        return 0;

    if (fwrite(screen, 1, SCREEN_SIZE, f)
        != SCREEN_SIZE)
    {
        fclose(f);
        return 0;
    }

    fclose(f);

    return 1;
}


/* ========================================================= */
/* File processing                                           */
/* ========================================================= */

static int process_file(const char *input_name,
                        const char *output_name,
                        const char *format,
                        int dithering,
                        Mutex *print_mutex)
{
    uint8_t *scr;
    FILE *f;
    long file_size;
    int result;

    scr = NULL;
    f = NULL;

    f = fopen(input_name, "rb");

    if (!f)
    {
        print_error(print_mutex,
                    "Cannot open input file: %s\n",
                    input_name);
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return 0;
    }

    file_size = ftell(f);

    if (file_size != SCR_SIZE)
    {
        print_error(print_mutex,
                    "Invalid SCR size: %s: %ld bytes, expected %d\n",
                    input_name,
                    file_size,
                    SCR_SIZE);

        fclose(f);
        return 0;
    }

    if (fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return 0;
    }

    scr = (uint8_t *)malloc(SCR_SIZE);

    if (!scr)
    {
        fclose(f);

        print_error(print_mutex,
                    "Out of memory: %s\n",
                    input_name);

        return 0;
    }

    if (fread(scr, 1, SCR_SIZE, f) != SCR_SIZE)
    {
        fclose(f);
        free(scr);

        print_error(print_mutex,
                    "Cannot read input file: %s\n",
                    input_name);

        return 0;
    }

    fclose(f);

    result = 0;


    /* ===================================================== */
    /* Standard ZX RGB PNG                                    */
    /* ===================================================== */

    if (strcmp(format, "png") == 0)
    {
        uint8_t *rgb;

        rgb = (uint8_t *)
            malloc((size_t)SCREEN_WIDTH *
                   SCREEN_HEIGHT *
                   3);

        if (!rgb)
        {
            print_error(print_mutex,
                        "Out of memory: %s\n",
                        input_name);

            free(scr);
            return 0;
        }

        zx_to_rgb(scr, rgb);

        result =
            write_png(output_name,
                      rgb,
                      SCREEN_WIDTH,
                      SCREEN_HEIGHT);

        free(rgb);

        if (!result)
        {
            print_error(print_mutex,
                        "Cannot write PNG: %s\n",
                        output_name);

            free(scr);
            return 0;
        }
    }


    /* ===================================================== */
    /* Standard ZX RGB QOI                                    */
    /* ===================================================== */

    else if (strcmp(format, "qoi") == 0)
    {
        uint8_t *rgb;
        qoi_desc desc;

        rgb = (uint8_t *)
            malloc((size_t)SCREEN_WIDTH *
                   SCREEN_HEIGHT *
                   3);

        if (!rgb)
        {
            print_error(print_mutex,
                        "Out of memory: %s\n",
                        input_name);

            free(scr);
            return 0;
        }

        zx_to_rgb(scr, rgb);

        desc.width = SCREEN_WIDTH;
        desc.height = SCREEN_HEIGHT;
        desc.channels = 3;
        desc.colorspace = QOI_SRGB;

        result =
            qoi_write(output_name,
                      rgb,
                      &desc);

        free(rgb);

        if (!result)
        {
            print_error(print_mutex,
                        "Cannot write QOI: %s\n",
                        output_name);

            free(scr);
            return 0;
        }
    }


    /* ===================================================== */
    /* Atari color reference                                  */
    /* ===================================================== */
    /*
     * -color
     *
     * ZX 256x192
     *       |
     *       v
     * Atari logical 160x240
     *       |
     *       v
     * RGB reference 320x240
     *
     * Every logical Atari pixel is represented by two
     * horizontal RGB pixels.
     *
     * The original 192 image lines occupy rows 24..215.
     * There are 24 black rows above and below.
     */

    else if (strcmp(format, "color") == 0)
    {
        uint8_t *rgb;
        unsigned unique_colors;

        rgb =
            (uint8_t *)malloc(
                (size_t)COLOR_PHYSICAL_WIDTH *
                COLOR_PHYSICAL_HEIGHT *
                3);

        if (!rgb)
        {
            print_error(print_mutex,
                        "Out of memory: %s\n",
                        input_name);

            free(scr);
            return 0;
        }

        result =
convert_scr_to_color_reference(
    scr,
    rgb,
    0,
    COLOR_RESIZE_NEAREST,
    &unique_colors);

        if (!result)
        {
            print_error(
                print_mutex,
                "Cannot convert %s: image uses more than 4 Atari colors\n",
                input_name);

            free(rgb);
            free(scr);
            return 0;
        }

        /*
         * Normally -color produces PNG.
         *
         * If the output filename has .qoi, write QOI instead.
         */
        if (has_extension(output_name, ".qoi") ||
            has_extension(output_name, ".QOI"))
        {
            qoi_desc desc;

            desc.width = COLOR_PHYSICAL_WIDTH;
            desc.height = COLOR_PHYSICAL_HEIGHT;
            desc.channels = 3;
            desc.colorspace = QOI_SRGB;

            result =
                qoi_write(output_name,
                          rgb,
                          &desc);
        }
        else
        {
            result =
                write_png(output_name,
                          rgb,
                          COLOR_PHYSICAL_WIDTH,
                          COLOR_PHYSICAL_HEIGHT);
        }

        free(rgb);

        if (!result)
        {
            print_error(print_mutex,
                        "Cannot write color image: %s\n",
                        output_name);

            free(scr);
            return 0;
        }
    }

/* ===================================================== */
/* Atari color reference - four-level grayscale          */
/* ===================================================== */

else if (strcmp(format, "colorn-grey") == 0)
{
    uint8_t *rgb;
    unsigned unique_colors;

    rgb =
        (uint8_t *)malloc(
            (size_t)COLOR_PHYSICAL_WIDTH *
            COLOR_PHYSICAL_HEIGHT *
            3);

    if (!rgb)
    {
        print_error(
            print_mutex,
            "Out of memory: %s\n",
            input_name);

        free(scr);
        return 0;
    }

    result =
        convert_scr_to_color_grey_reference(
            scr,
            rgb,
            &unique_colors);

    if (!result)
    {
        print_error(
            print_mutex,
            "Cannot convert %s\n",
            input_name);

        free(rgb);
        free(scr);
        return 0;
    }

    if (has_extension(output_name, ".qoi") ||
        has_extension(output_name, ".QOI"))
    {
        qoi_desc desc;

        desc.width = COLOR_PHYSICAL_WIDTH;
        desc.height = COLOR_PHYSICAL_HEIGHT;
        desc.channels = 3;
        desc.colorspace = QOI_SRGB;

        result =
            qoi_write(
                output_name,
                rgb,
                &desc);
    }
    else
    {
        result =
            write_png(
                output_name,
                rgb,
                COLOR_PHYSICAL_WIDTH,
                COLOR_PHYSICAL_HEIGHT);
    }

    free(rgb);

    if (!result)
    {
        print_error(
            print_mutex,
            "Cannot write grayscale image: %s\n",
            output_name);

        free(scr);
        return 0;
    }
}
else if (strcmp(format, "colorn-grey-smart") == 0)
{
    uint8_t *rgb;
    unsigned unique_colors;

    rgb =
        (uint8_t *)malloc(
            (size_t)COLOR_PHYSICAL_WIDTH *
            COLOR_PHYSICAL_HEIGHT *
            3);

    if (!rgb)
    {
        print_error(
            print_mutex,
            "Out of memory: %s\n",
            input_name);

        free(scr);
        return 0;
    }

    result =
        convert_scr_to_color_grey_smart_reference(
            scr,
            rgb,
            &unique_colors);

    if (!result)
    {
        print_error(
            print_mutex,
            "Cannot convert %s\n",
            input_name);

        free(rgb);
        free(scr);
        return 0;
    }

    if (has_extension(output_name, ".qoi") ||
        has_extension(output_name, ".QOI"))
    {
        qoi_desc desc;

        desc.width = COLOR_PHYSICAL_WIDTH;
        desc.height = COLOR_PHYSICAL_HEIGHT;
        desc.channels = 3;
        desc.colorspace = QOI_SRGB;

        result =
            qoi_write(
                output_name,
                rgb,
                &desc);
    }
    else
    {
        result =
            write_png(
                output_name,
                rgb,
                COLOR_PHYSICAL_WIDTH,
                COLOR_PHYSICAL_HEIGHT);
    }

    free(rgb);

    if (!result)
    {
        print_error(
            print_mutex,
            "Cannot write grayscale image: %s\n",
            output_name);

        free(scr);
        return 0;
    }
}

    /* ===================================================== */
    /* Atari color reference - nearest GTIA                  */
    /* ===================================================== */
    /*
     * -colorn
     *
     * Same 320x240 geometry as -color.
     *
     * The only difference is the ZX -> GTIA palette mapping:
     * nearest GTIA color is calculated for every ZX palette
     * entry.
     */

else if (strcmp(format, "colorn") == 0 ||
         strcmp(format, "colorn-bres") == 0 ||
         strcmp(format, "colorn-area") == 0)
{
    uint8_t *rgb;
    unsigned unique_colors;
    int resize_mode;

    if (strcmp(format, "colorn-bres") == 0)
        resize_mode = COLOR_RESIZE_BRES;
    else if (strcmp(format, "colorn-area") == 0)
        resize_mode = COLOR_RESIZE_AREA;
    else
        resize_mode = COLOR_RESIZE_NEAREST;

    rgb =
        (uint8_t *)malloc(
            (size_t)COLOR_PHYSICAL_WIDTH *
            COLOR_PHYSICAL_HEIGHT *
            3);

    if (!rgb)
    {
        print_error(
            print_mutex,
            "Out of memory: %s\n",
            input_name);

        free(scr);
        return 0;
    }

    result =
        convert_scr_to_color_reference(
            scr,
            rgb,
            1,
            resize_mode,
            &unique_colors);

    if (!result)
    {
        print_error(
            print_mutex,
            "Cannot convert %s: image uses more than 4 Atari colors\n",
            input_name);

        free(rgb);
        free(scr);
        return 0;
    }

    if (has_extension(output_name, ".qoi") ||
        has_extension(output_name, ".QOI"))
    {
        qoi_desc desc;

        desc.width = COLOR_PHYSICAL_WIDTH;
        desc.height = COLOR_PHYSICAL_HEIGHT;
        desc.channels = 3;
        desc.colorspace = QOI_SRGB;

        result =
            qoi_write(
                output_name,
                rgb,
                &desc);
    }
    else
    {
        result =
            write_png(
                output_name,
                rgb,
                COLOR_PHYSICAL_WIDTH,
                COLOR_PHYSICAL_HEIGHT);
    }

    free(rgb);

    if (!result)
    {
        print_error(
            print_mutex,
            "Cannot write color image: %s\n",
            output_name);

        free(scr);
        return 0;
    }
}


    /* ===================================================== */
    /* Atari BIN                                               */
    /* ===================================================== */

    else if (strcmp(format, "bin") == 0)
    {
        uint8_t screen[SCREEN_SIZE];

        if (dithering)
        {
            convert_scr_to_atari_dither(scr,
                                        screen,
                                        dithering);
        }
        else
        {
            convert_scr_to_atari(scr,
                                 screen);
        }

        result =
            write_bin(output_name,
                      screen);

        if (!result)
        {
            print_error(print_mutex,
                        "Cannot write BIN: %s\n",
                        output_name);

            free(scr);
            return 0;
        }
    }


    /* ===================================================== */
    /* Atari XEX                                               */
    /* ===================================================== */

    else if (strcmp(format, "xex") == 0)
    {
        size_t memory_size;
        size_t screen_offset;
        uint8_t *memory;

        memory_size =
            PROGRAM_END - PROGRAM_START + 1;

        memory =
            (uint8_t *)malloc(memory_size);

        if (!memory)
        {
            print_error(print_mutex,
                        "Out of memory: %s\n",
                        input_name);

            free(scr);
            return 0;
        }

        /*
         * Existing XEX implementation is intentionally
         * left unchanged.
         */
        build_atari_memory(scr,
                           memory,
                           dithering);

        screen_offset =
            SCREEN_ADDR - PROGRAM_START;

        if (screen_offset + SCREEN_SIZE >
            memory_size)
        {
            print_error(print_mutex,
                        "Internal error: screen outside memory image\n");

            free(memory);
            free(scr);
            return 0;
        }

        result =
            write_xex(output_name,
                      memory);

        free(memory);

        if (!result)
        {
            print_error(print_mutex,
                        "Cannot write XEX: %s\n",
                        output_name);

            free(scr);
            return 0;
        }
    }


    /* ===================================================== */
    /* Unknown format                                         */
    /* ===================================================== */

    else
    {
        print_error(print_mutex,
                    "Unknown output format: %s\n",
                    format);

        free(scr);
        return 0;
    }

    free(scr);

    return 1;
}


/* ========================================================= */
/* Job queue                                                 */
/* ========================================================= */

typedef struct Job
{
    char *input;
    char *output;
    struct Job *next;
} Job;


typedef struct
{
    Job *head;
    Job *tail;
    int stop;

    Mutex mutex;
    Cond cond;
} JobQueue;


static void queue_init(JobQueue *q)
{
    q->head = NULL;
    q->tail = NULL;
    q->stop = 0;

    mutex_init(&q->mutex);
    cond_init(&q->cond);
}


static void queue_destroy(JobQueue *q)
{
    Job *job;
    Job *next;

    job = q->head;

    while (job)
    {
        next = job->next;

        free(job->input);
        free(job->output);
        free(job);

        job = next;
    }

    q->head = NULL;
    q->tail = NULL;

    cond_destroy(&q->cond);
    mutex_destroy(&q->mutex);
}


static int queue_push(JobQueue *q,
                      char *input,
                      char *output)
{
    Job *job;

    job = (Job *)malloc(sizeof(Job));

    if (!job)
        return 0;

    job->input = input;
    job->output = output;
    job->next = NULL;

    mutex_lock(&q->mutex);

    if (q->tail)
        q->tail->next = job;
    else
        q->head = job;

    q->tail = job;

    cond_signal(&q->cond);

    mutex_unlock(&q->mutex);

    return 1;
}


static Job *queue_pop(JobQueue *q)
{
    Job *job;

    mutex_lock(&q->mutex);

    while (!q->head && !q->stop)
        cond_wait(&q->cond, &q->mutex);

    job = q->head;

    if (job)
    {
        q->head = job->next;

        if (!q->head)
            q->tail = NULL;
    }

    mutex_unlock(&q->mutex);

    return job;
}


static void queue_stop(JobQueue *q)
{
    mutex_lock(&q->mutex);

    q->stop = 1;

    cond_broadcast(&q->cond);

    mutex_unlock(&q->mutex);
}


/* ========================================================= */
/* Workers                                                   */
/* ========================================================= */

typedef struct
{
    JobQueue *queue;

    const char *format;
    int dithering;
    int quiet;

    int *errors;

    Mutex *result_mutex;
    Mutex *print_mutex;

} WorkerArgs;


static void worker_run(WorkerArgs *args)
{
    Job *job;

    for (;;)
    {
        job = queue_pop(args->queue);

        if (!job)
            break;

        if (!process_file(job->input,
                          job->output,
                          args->format,
                          args->dithering,
                          args->print_mutex))
        {
            mutex_lock(args->result_mutex);
            ++(*args->errors);
            mutex_unlock(args->result_mutex);
        }
        else if (!args->quiet)
        {
            mutex_lock(args->print_mutex);

            printf("OK: %s -> %s\n",
                   job->input,
                   job->output);

            mutex_unlock(args->print_mutex);
        }

        free(job->input);
        free(job->output);
        free(job);
    }
}


#ifdef _WIN32

static DWORD WINAPI worker_thread(LPVOID arg)
{
    worker_run((WorkerArgs *)arg);
    return 0;
}

#else

static void *worker_thread(void *arg)
{
    worker_run((WorkerArgs *)arg);
    return NULL;
}

#endif


static int thread_create(Thread *thread,
                          WorkerArgs *args)
{
#ifdef _WIN32

    *thread =
        CreateThread(NULL,
                     0,
                     worker_thread,
                     args,
                     0,
                     NULL);

    return *thread != NULL;

#else

    return
        pthread_create(thread,
                       NULL,
                       worker_thread,
                       args) == 0;

#endif
}


static void thread_join(Thread thread)
{
#ifdef _WIN32

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);

#else

    pthread_join(thread, NULL);

#endif
}


/* ========================================================= */
/* CPU count                                                 */
/* ========================================================= */

static int cpu_count(void)
{
#ifdef _WIN32

    SYSTEM_INFO info;

    GetSystemInfo(&info);

    if (info.dwNumberOfProcessors < 1)
        return 1;

    return (int)info.dwNumberOfProcessors;

#else

    long n;

    n = sysconf(_SC_NPROCESSORS_ONLN);

    if (n < 1)
        return 1;

    if (n > 1024)
        n = 1024;

    return (int)n;

#endif
}


/* ========================================================= */
/* Usage                                                     */
/* ========================================================= */

static void usage(const char *program)
{
    printf(
        "Usage:\n"
        "  %s input.scr -xex output.xex\n"
        "  %s input.scr -bin output.bin\n"
        "  %s input.scr -png output.png\n"
        "  %s input.scr -qoi output.qoi\n"
        "  %s input.scr -color output.png\n"
        "  %s input.scr -colorn output.png\n"
	"  %s input.scr -colorn-bres output.png\n"
	"  %s input.scr -colorn-area output.png\n"
	"  %s input.scr -colorn-grey output.png\n"
	"  %s input.scr -colorn-grey-smart output.png\n"
        "  %s *.scr -xex\n"
        "  %s *.scr -png -j 8\n"
        "  %s input.scr -d -xex output.xex\n"
        "  %s input.scr -dw -xex output.xex\n"
        "  %s input.scr -o output.xex\n"
        "\n"
        "Options:\n"
        "  -xex FILE   create Atari XEX\n"
        "  -bin FILE   create raw Atari screen\n"
        "  -png FILE   create RGB PNG\n"
        "  -qoi FILE   create RGB QOI\n"
        "  -color      create 320x240 Atari color reference image\n"
        "  -colorn     create 320x240 reference using nearest GTIA colors\n"
	"  -colorn-bres  nearest GTIA colors with Bresenham resize\n"
	"  -colorn-area  nearest GTIA colors with area resize\n"
	"  -colorn-grey  four most popular ZX components as grayscale\n"
	"  -colorn-grey-smart  perceptually weighted four-level grayscale\n"
        "  -d          Floyd-Steinberg dithering\n"
        "  -dw         dithering, preserve ZX white\n"
        "  -do         ordered diagonal dithering\n"
        "  -o FILE     output file, format from extension\n"
        "  -j N        number of worker threads\n"
        "  -q          quiet\n"
        "  -h          show this help\n"
        "\n"
        "Multiple input files may be specified.\n"
        "Wildcard masks * and ? are supported.\n",
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program,
        program);
}


/* ========================================================= */
/* Main                                                      */
/* ========================================================= */

int main(int argc, char **argv)
{
    StringList files;

    const char *format;
    const char *explicit_output;

    int dithering;
    int quiet;
    int requested_threads;

    int i;

    string_list_init(&files);

    format = NULL;
    explicit_output = NULL;

    dithering = 0;
    quiet = 0;
    requested_threads = 0;

    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }

    for (i = 1; i < argc; ++i)
    {
        const char *arg;

        arg = argv[i];

        if (strcmp(arg, "-d") == 0)
        {
            dithering = 1;
            continue;
        }

        if (strcmp(arg, "-dw") == 0)
        {
            dithering = 2;
            continue;
        }

        if (!strcmp(argv[i], "-do"))
        {
            dithering = 3;
            continue;
        }

        if (strcmp(arg, "-q") == 0)
        {
            quiet = 1;
            continue;
        }

        if (strcmp(arg, "-h") == 0 ||
            strcmp(arg, "--help") == 0)
        {
            usage(argv[0]);
            string_list_free(&files);
            return 0;
        }

        if (strcmp(arg, "-j") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr,
                        "Missing thread count after -j\n");

                string_list_free(&files);
                return 1;
            }

            requested_threads =
                atoi(argv[++i]);

            if (requested_threads < 1)
            {
                fprintf(stderr,
                        "Invalid thread count\n");

                string_list_free(&files);
                return 1;
            }

            continue;
        }

        if (strcmp(arg, "-xex") == 0)
        {
            format = "xex";

            if (i + 1 < argc &&
                argv[i + 1][0] != '-')
            {
                explicit_output = argv[++i];
            }

            continue;
        }

        if (strcmp(arg, "-bin") == 0)
        {
            format = "bin";

            if (i + 1 < argc &&
                argv[i + 1][0] != '-')
            {
                explicit_output = argv[++i];
            }

            continue;
        }

        if (strcmp(arg, "-color") == 0)
        {
            format = "color";

            if (i + 1 < argc &&
                argv[i + 1][0] != '-')
            {
                explicit_output = argv[++i];
            }

            continue;
        }

        if (strcmp(arg, "-colorn") == 0)
        {
            format = "colorn";

            if (i + 1 < argc &&
                argv[i + 1][0] != '-')
            {
                explicit_output = argv[++i];
            }

            continue;
        }
if (strcmp(arg, "-colorn-bres") == 0)
{
    format = "colorn-bres";

    if (i + 1 < argc &&
        argv[i + 1][0] != '-')
    {
        explicit_output = argv[++i];
    }

    continue;
}

if (strcmp(arg, "-colorn-area") == 0)
{
    format = "colorn-area";

    if (i + 1 < argc &&
        argv[i + 1][0] != '-')
    {
        explicit_output = argv[++i];
    }

    continue;
}
if (strcmp(arg, "-colorn-grey") == 0)
{
    format = "colorn-grey";

    if (i + 1 < argc &&
        argv[i + 1][0] != '-')
    {
        explicit_output = argv[++i];
    }

    continue;
}
if (strcmp(arg, "-colorn-grey-smart") == 0)
{
    format = "colorn-grey-smart";

    if (i + 1 < argc &&
        argv[i + 1][0] != '-')
    {
        explicit_output = argv[++i];
    }

    continue;
}
        if (strcmp(arg, "-png") == 0)
        {
            format = "png";

            if (i + 1 < argc &&
                argv[i + 1][0] != '-')
            {
                explicit_output = argv[++i];
            }

            continue;
        }

        if (strcmp(arg, "-qoi") == 0)
        {
            format = "qoi";

            if (i + 1 < argc &&
                argv[i + 1][0] != '-')
            {
                explicit_output = argv[++i];
            }

            continue;
        }

        if (strcmp(arg, "-o") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr,
                        "Missing output filename\n");

                string_list_free(&files);
                return 1;
            }

            explicit_output = argv[++i];
            continue;
        }

        if (arg[0] == '-')
        {
            fprintf(stderr,
                    "Unknown option: %s\n",
                    arg);

            string_list_free(&files);
            return 1;
        }

        if (!collect_pattern(arg, &files))
        {
            /*
             * If it is an explicit filename, keep it so
             * that the normal input error is reported later.
             */
            if (!string_list_add(&files, arg))
            {
                fprintf(stderr,
                        "Out of memory\n");

                string_list_free(&files);
                return 1;
            }
        }
    }

    if (files.n == 0)
    {
        fprintf(stderr,
                "No input SCR files found\n");

        string_list_free(&files);
        return 1;
    }


    /*
     * Determine format from explicit output when necessary.
     */

    if (!format && explicit_output)
        format = extension_format(explicit_output);


    /*
     * With no explicit format and multiple files,
     * default to XEX.
     */

    if (!format)
    {
        if (files.n == 1 && explicit_output)
        {
            format = extension_format(explicit_output);
        }
        else
        {
            format = "xex";
        }
    }


    /*
     * Explicit output is valid only for one input.
     */

    if (explicit_output && files.n != 1)
    {
        fprintf(stderr,
                "Explicit output file can only be used with one input file\n");

        string_list_free(&files);
        return 1;
    }


    /*
     * Single file:
     *
     * Do not create worker threads.
     */

    if (files.n == 1)
    {
        char *output;
        int ok;

        if (explicit_output)
        {
            output = dup_string(explicit_output);
        }
        else
        {
            const char *ext;

            if (strcmp(format, "xex") == 0)
                ext = ".xex";
            else if (strcmp(format, "bin") == 0)
                ext = ".bin";
            else if (strcmp(format, "qoi") == 0)
                ext = ".qoi";
            else if (strcmp(format, "color") == 0)
                ext = ".png";
else if (strcmp(format, "colorn") == 0)
    ext = ".png";
else if (strcmp(format, "colorn-bres") == 0)
    ext = ".png";
else if (strcmp(format, "colorn-area") == 0)
    ext = ".png";
else if (strcmp(format, "colorn-grey") == 0)
    ext = ".png";
else if (strcmp(format, "colorn-grey-smart") == 0)
    ext = ".png";
            else
                ext = ".png";

            output =
                replace_extension(files.items[0], ext);
        }

        if (!output)
        {
            fprintf(stderr,
                    "Out of memory\n");

            string_list_free(&files);
            return 1;
        }

        ok =
            process_file(files.items[0],
                         output,
                         format,
                         dithering,
                         NULL);

        if (ok && !quiet)
        {
            printf("OK: %s -> %s\n",
                   files.items[0],
                   output);
        }

        free(output);
        string_list_free(&files);

        return ok ? 0 : 1;
    }


    /* ===================================================== */
    /* Multi-file worker pool                                */
    /* ===================================================== */

    {
        JobQueue queue;

        Mutex result_mutex;
        Mutex print_mutex;

        WorkerArgs args;

        Thread *threads;

        int nthreads;
        int created;

        int worker_errors;
        int setup_error;

        size_t n;

        queue_init(&queue);

        mutex_init(&result_mutex);
        mutex_init(&print_mutex);

        worker_errors = 0;
        setup_error = 0;
        created = 0;

        nthreads =
            requested_threads > 0
                ? requested_threads
                : cpu_count();

        if (nthreads > (int)files.n)
            nthreads = (int)files.n;

        if (nthreads < 1)
            nthreads = 1;

        threads =
            (Thread *)malloc((size_t)nthreads *
                             sizeof(Thread));

        if (!threads)
        {
            fprintf(stderr,
                    "Out of memory\n");

            mutex_destroy(&print_mutex);
            mutex_destroy(&result_mutex);
            queue_destroy(&queue);
            string_list_free(&files);

            return 1;
        }

        args.queue = &queue;
        args.format = format;
        args.dithering = dithering;
        args.quiet = quiet;
        args.errors = &worker_errors;
        args.result_mutex = &result_mutex;
        args.print_mutex = &print_mutex;


        /*
         * Create workers BEFORE putting jobs into the queue.
         */

        for (i = 0; i < nthreads; ++i)
        {
            if (!thread_create(&threads[created],
                               &args))
            {
                fprintf(stderr,
                        "Cannot create worker thread %d\n",
                        i);

                setup_error = 1;
                break;
            }

            ++created;
        }


        /*
         * Enqueue jobs only if worker creation succeeded.
         */

        if (!setup_error)
        {
            for (n = 0; n < files.n; ++n)
            {
                const char *ext;
                char *output;
                char *input;

                if (strcmp(format, "xex") == 0)
                    ext = ".xex";
                else if (strcmp(format, "bin") == 0)
                    ext = ".bin";
                else if (strcmp(format, "qoi") == 0)
                    ext = ".qoi";
                else if (strcmp(format, "color") == 0)
                    ext = ".png";
else if (strcmp(format, "colorn") == 0)
    ext = ".png";
else if (strcmp(format, "colorn-bres") == 0)
    ext = ".png";
else if (strcmp(format, "colorn-area") == 0)
    ext = ".png";
else if (strcmp(format, "colorn-grey") == 0)
    ext = ".png";
else if (strcmp(format, "colorn-grey-smart") == 0)
    ext = ".png";
                else
                    ext = ".png";

                input =
                    dup_string(files.items[n]);

                if (!input)
                {
                    setup_error = 1;
                    break;
                }

                output =
                    replace_extension(files.items[n],
                                      ext);

                if (!output)
                {
                    free(input);
                    setup_error = 1;
                    break;
                }

                if (!queue_push(&queue,
                                input,
                                output))
                {
                    free(input);
                    free(output);

                    setup_error = 1;
                    break;
                }
            }
        }


        /*
         * Wake all workers.
         *
         * They first drain any already queued jobs,
         * then terminate.
         */

        queue_stop(&queue);

        for (i = 0; i < created; ++i)
            thread_join(threads[i]);

        free(threads);

        queue_destroy(&queue);

        mutex_destroy(&print_mutex);
        mutex_destroy(&result_mutex);

        string_list_free(&files);

        return (setup_error || worker_errors) ? 1 : 0;
    }
}