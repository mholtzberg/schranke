/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 UVC Ingenieure http://uvc.de/
 * Author: Max Holtzberg <mh@uvc.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "ring.h"


void ring_init(struct ring *ring)
{
    ring->head = 0;
    ring->tail = 0;
}

void ring_enq(struct ring *ring, char c)
{
    ring->buf[ring->tail] = c;
    ring->tail = (ring->tail + 1) % RING_SIZE;

    if (ring->head == ring->tail)
        ring->head = (ring->head + 1) % RING_SIZE;
}

bool ring_empty(struct ring *ring)
{
    return ring->head == ring->tail;
}

int ring_deq(struct ring *ring)
{
    int ret = -1;

    if (!ring_empty(ring)) {
        ret = ring->buf[ring->head];
        ring->head = (ring->head + 1) % RING_SIZE;
    }

    return ret;
}
