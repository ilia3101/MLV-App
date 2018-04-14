/*!
 * \file blur_threaded.c
 * \author masc4ii & ilia3101
 * \copyright 2018
 * \brief a blur using threads
 */

#include "blur_threaded.h"
#include <pthread.h>

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

uint16_t * __restrict m_in;
uint16_t * __restrict m_temp;
int m_width;
int m_height;
int m_radius;
uint8_t m_threads;

void* horizontal_blur(void* tnum)
{
    /* Row length */
    int rl = m_width * 3;
    int radius_x = m_radius*3;
    int blur_diameter = m_radius*2+1;
    int thread_num = (int)tnum;
    int start = (m_height / m_threads) * thread_num;
    int end;
    if (thread_num == m_threads - 1)
        end = m_height;
    else
        end = (m_height / m_threads)*(thread_num + 1);

    /* Offset - do twice on channel '1' and '2' (Cb and Cr) */
    int limit_x = (m_width-m_radius-1)*3;
    for (int offset = 0; offset < 3; ++offset)
    {
        /* Horizontal blur */
        for (int y = start; y < end; ++y) /* rows */
        {
            uint16_t * temp_row = m_temp + (y * rl)+offset; /* current row ouptut */
            uint16_t * row = m_in + (y * rl)+offset; /* current row */

            int sum = row[0] * blur_diameter;

            /* Split in to 3 parts to avoid MIN/MAX */
            for (int x = -radius_x; x < radius_x; x+=3)
            {
                sum -= row[MAX(x-radius_x, 0)];
                sum += row[x+radius_x+3];
                temp_row[MAX(x, 0)] = sum / blur_diameter;
            }
            for (int x = radius_x; x < limit_x; x+=3)
            {
                sum -= row[x-radius_x];
                sum += row[x+radius_x+3];
                temp_row[x] = sum / blur_diameter;
            }
            for (int x = limit_x; x < rl; x+=3)
            {
                sum -= row[x-radius_x];
                sum += row[MIN(x+radius_x+3, rl-3)];
                temp_row[x] = sum / blur_diameter;
            }
        }
    }

    return NULL;
}

void* vertical_blur(void* tnum)
{
    //* Row length */
    int rl = m_width * 3;
    int blur_diameter = m_radius*2+1;
    int thread_num = (int)tnum;
    int start = (m_width / m_threads) * thread_num;
    int end;
    if (thread_num == m_threads - 1)
        end = m_width;
    else
        end = (m_width / m_threads)*(thread_num + 1);

    for (int offset =0; offset < 3; ++offset)
    {
        /* Vertical blur */
        int limit_y = m_height-m_radius-1;
        for (int x = start; x < end; ++x) /* columns */
        {
            uint16_t * temp_col = m_in + (x*3);
            uint16_t * col = m_temp + (x*3);

            int sum = m_temp[x*3+offset] * blur_diameter;

            for (int y = -m_radius; y < m_radius; ++y)
            {
                sum -= col[MAX((y-m_radius), 0)*rl+offset];
                sum += col[(y+m_radius+1)*rl+offset];
                temp_col[MAX(y, 0)*rl+offset] = sum / blur_diameter;
            }
            {
                uint16_t * minus = col + (offset);
                uint16_t * plus = col + ((m_radius*2+1)*rl + offset);
                uint16_t * temp = temp_col + (m_radius*rl + offset);
                uint16_t * end = temp_col + (limit_y*rl + offset);
                do {
                    sum -= *minus;
                    sum += *plus;
                    *temp = sum / blur_diameter;
                    minus += rl;
                    plus += rl;
                    temp += rl;
                } while (temp < end);
            }
            for (int y = limit_y; y < m_height; ++y)
            {
                sum -= col[(y-m_radius)*rl+offset];
                sum += col[MIN((y+m_radius+1), m_height-1)*rl+offset];
                temp_col[y*rl+offset] = sum / blur_diameter;
            }
        }
    }

    return NULL;
}

/* Box blur threaded*/
void blur_image_threaded( uint16_t * __restrict in,
                 uint16_t * __restrict temp,
                 int width, int height, int radius,
                 uint8_t threads )
{
    m_in = in;
    m_temp = temp;
    m_width = width;
    m_height = height;
    m_radius = radius;
    m_threads = threads;
    int i;

    pthread_t thread[threads];
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    /* Create and run the threads */
    for (i=0; i<threads; i++)
            pthread_create(&thread[i], &attr, horizontal_blur, (void *) i);

    /* Join the threads - barrier*/
    for (i=0; i<threads; i++)
            pthread_join(thread[i], NULL);

    for (i=0; i<threads; i++)
            pthread_create(&thread[i], &attr, vertical_blur, (void *) i);

    for (i=0; i<threads; i++)
            pthread_join(thread[i], NULL);

    return;
}
