/*
 * main.c
 *
 *  Created on: 15 Nov 2014
 *      Author: Mr_Halfword
 */

#include <stdlib.h>
#include <stdio.h>

#include "cmem_drv.h"

#define NUM_BUFFERS 8
#define BUFFER_SIZE 16384

int main (int argc, char *argv[])
{
    int32_t rc;
    cmem_host_buf_desc_t buffer_descs[NUM_BUFFERS];
    int buffer_index;
    char *buffer_text;

    rc = cmem_drv_open ();
    if (rc != 0)
    {
        fprintf (stderr, "cmem_drv_open failed\n");
        return EXIT_FAILURE;
    }

    rc = cmem_drv_alloc (NUM_BUFFERS, BUFFER_SIZE, buffer_descs);
    if (rc != 0)
    {
        fprintf (stderr, "cmem_drv_alloc failed\n");
        return EXIT_FAILURE;
    }

    for (buffer_index = 0; buffer_index < NUM_BUFFERS; buffer_index++)
    {
        buffer_text = (char *) buffer_descs[buffer_index].userAddr;
        sprintf (buffer_text, "This is a user string placed at virtual address %p physical address 0x%lx\n",
                 buffer_descs[buffer_index].userAddr, buffer_descs[buffer_index].physAddr);
        printf ("%s", buffer_text);
    }

    rc = cmem_drv_free (NUM_BUFFERS, buffer_descs);
    if (rc != 0)
    {
        fprintf (stderr, "cmem_drv_alloc failed\n");
        return EXIT_FAILURE;
    }

    rc = cmem_drv_close ();
    if (rc != 0)
    {
        fprintf (stderr, "cmem_drv_close failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
