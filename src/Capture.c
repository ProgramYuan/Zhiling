/*********************************************************************
ALSA 简单的录音功能 capture 捕获
*********************************************************************/

/* Use the newer ALSA API */
#define ALSA_PCM_NEW_HW_PARAMS_API

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>


int main() {
    long loops;
    int rc;
    int size;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *params;
    unsigned int val;
    int dir;
    snd_pcm_uframes_t frames;
    char *buffer;

    int fdnew;
    int size_buf;

    fdnew = open("./write.pcm",O_WRONLY|O_CREAT|O_TRUNC,S_IREAD|  
S_IWRITE);  
    if(fdnew == -1)  
    {  
        printf("Error open file.\n");  
        return -1;  
    }  


    /* Open PCM device for recording (capture). */
    /* 打开 PCM capture 捕获设备 */
    rc = snd_pcm_open(&handle, "default",
                        SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        fprintf(stderr,
                "unable to open pcm device: %s\n",
                snd_strerror(rc));
        exit(1);
    }

    /* Allocate a hardware parameters object. */
    /* 分配一个硬件参数结构体 */
    snd_pcm_hw_params_alloca(&params);

    /* Fill it in with default values. */
    /* 使用默认参数 */
    snd_pcm_hw_params_any(handle, params);

    /* Set the desired hardware parameters. */

    /* Interleaved mode */
    snd_pcm_hw_params_set_access(handle, params,
                          SND_PCM_ACCESS_RW_INTERLEAVED);

    /* Signed 16-bit little-endian format */
    /* 16位 小端 */
    snd_pcm_hw_params_set_format(handle, params,
                                  SND_PCM_FORMAT_S16_LE);

    /* Two channels (stereo) */
    /* 双通道 */
    snd_pcm_hw_params_set_channels(handle, params, 1);

    /* 44100 bits/second sampling rate (CD quality) */
    /* 采样率 */
    //val = 44100;
    val = 16000;
    snd_pcm_hw_params_set_rate_near(handle, params,
                                      &val, &dir);

    /* Set period size to 32 frames. */
    /* 一个周期有 32 帧 */
    frames = 32;
    snd_pcm_hw_params_set_period_size_near(handle,
                                  params, &frames, &dir);

    /* Write the parameters to the driver */
    /* 参数生效 */
    rc = snd_pcm_hw_params(handle, params);
    if (rc < 0) {
        fprintf(stderr,
                "unable to set hw parameters: %s\n",
                snd_strerror(rc));
        exit(1);
    }

    /* Use a buffer large enough to hold one period */
    /* 得到一个周期的数据大小 */
    snd_pcm_hw_params_get_period_size(params,
                                          &frames, &dir);
    /* 16位 双通道，所以要 *4 */
    size = frames * 2; /* 2 bytes/sample, 2 channels */
    buffer = (char *) malloc(size);

    /* We want to loop for 5 seconds */
    /* 等到一个周期的时间长度 */
    snd_pcm_hw_params_get_period_time(params,
                                             &val, &dir);
    loops = 5000000 / val;

    while (loops > 0) {
        loops--;
        /* 捕获数据 */
        rc = snd_pcm_readi(handle, buffer, frames);
        if (rc == -EPIPE) {
            /* EPIPE means overrun */
            fprintf(stderr, "overrun occurred\n");
            snd_pcm_prepare(handle);
        } else if (rc < 0) {
            fprintf(stderr,
                  "error from read: %s\n",
                  snd_strerror(rc));
        } else if (rc != (int)frames) {
            fprintf(stderr, "short read, read %d frames\n", rc);
        }

        /* 写入到标准输出中去 */
        rc = write(1, buffer, size);
            
        if (rc != size)
          fprintf(stderr,
                  "short write: wrote %d bytes\n", rc);
    }

    size_buf=sizeof(buffer);
    write(fdnew,buffer,size_buf);
    close(fdnew);

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    free(buffer);
    return 0;
}
