/**
 *
 * This file is part of CaesiumPH.
 *
 * CaesiumPH - A Caesium version featuring lossless JPEG optimization/compression
 * for photographers and webmasters.
 *
 * Copyright (C) 2016 - Matteo Paonessa
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.
 * If not, see <http://www.gnu.org/licenses/>
 *
 */

#include <setjmp.h>
#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <QDebug>

#include "lossless.h"
#include "caesiumph.h"

struct jpeg_decompress_struct cclt_get_markers(char* input) {
    FILE* fp;
    struct jpeg_decompress_struct einfo;
    struct jpeg_error_mgr eerr;
    einfo.err = jpeg_std_error(&eerr);

    jpeg_create_decompress(&einfo);

    //Open the input file
    fp = fopen(input, "r");

    //Check for errors
    if (fp == NULL) {
        qCritical() << "Failed to open exif file" << input;
    }

    //Create the IO istance for the input file
    jpeg_stdio_src(&einfo, fp);

    //Save EXIF info
    for (int m = 0; m < 16; m++) {
        jpeg_save_markers(&einfo, JPEG_APP0 + m, 0xFFFF);
    }

    jpeg_read_header(&einfo, TRUE);

    fclose(fp);

    return einfo;
}

void jcopy_markers_execute (j_decompress_ptr srcinfo, j_compress_ptr dstinfo) {
  jpeg_saved_marker_ptr marker;

  for (marker = srcinfo->marker_list; marker != NULL; marker = marker->next) {
    if (dstinfo->write_JFIF_header &&
        marker->marker == JPEG_APP0 &&
        marker->data_length >= 5 &&
        GETJOCTET(marker->data[0]) == 0x4A &&
        GETJOCTET(marker->data[1]) == 0x46 &&
        GETJOCTET(marker->data[2]) == 0x49 &&
        GETJOCTET(marker->data[3]) == 0x46 &&
        GETJOCTET(marker->data[4]) == 0)
      continue;
    if (dstinfo->write_Adobe_marker &&
        marker->marker == JPEG_APP0+14 &&
        marker->data_length >= 5 &&
        GETJOCTET(marker->data[0]) == 0x41 &&
        GETJOCTET(marker->data[1]) == 0x64 &&
        GETJOCTET(marker->data[2]) == 0x6F &&
        GETJOCTET(marker->data[3]) == 0x62 &&
        GETJOCTET(marker->data[4]) == 0x65)
      continue;
    jpeg_write_marker(dstinfo, marker->marker,
                      marker->data, marker->data_length);
  }
}

extern int cclt_optimize(char* input_file, char* output_file, int exif_flag, int progressive_flag, char* exif_src) {
    //File pointer for both input and output
    FILE* fp;

    //Those will hold the input/output structs
    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;

    //Error handling
    struct jpeg_error_mgr jsrcerr, jdsterr;

    //Input/Output array coefficents
    jvirt_barray_ptr* src_coef_arrays;
    jvirt_barray_ptr* dst_coef_arrays;

    //Set errors and create the compress/decompress istances
    srcinfo.err = jpeg_std_error(&jsrcerr);
    jpeg_create_decompress(&srcinfo);
    dstinfo.err = jpeg_std_error(&jdsterr);
    jpeg_create_compress(&dstinfo);

    //Open the input file
    fp = fopen(input_file, "rb");

    qInfo() << "Compressing" << input_file;

    //Check for errors
    if (fp == NULL) {
        qCritical() << "Failed to open file" << input_file;
        return -1;
    }

    //Create the IO istance for the input file
    jpeg_stdio_src(&srcinfo, fp);

    qInfo() << "Save EXIF info";

    //Save EXIF info
    if (exif_flag == 2) {
        for (int m = 0; m < 16; m++) {
            jpeg_save_markers(&srcinfo, JPEG_APP0 + m, 0xFFFF);
        }
    }

    //Read the input headers
    (void) jpeg_read_header(&srcinfo, TRUE);

    //Read input coefficents
    src_coef_arrays = jpeg_read_coefficients(&srcinfo);

    //Copy parameters
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    //Set coefficents array to be the same
    dst_coef_arrays = src_coef_arrays;

    //We don't need the input file anymore
    fclose(fp);

    qInfo() << "Input file read succesfully";

    //Open the output one instead
    fp = fopen(output_file, "wb");
    //Check for errors
    if (fp == NULL) {
        qCritical() << "Failed to open output file" << output_file;
        return -1;
    }

    //CRITICAL - This is the optimization step
    dstinfo.optimize_coding = TRUE;

    //Progressive
    if (progressive_flag) {        
        jpeg_simple_progression(&dstinfo);
    } else {
        //Outputs a baseline image
        dstinfo.scan_info = NULL;
    }

    //Set the output file parameters
    jpeg_stdio_dest(&dstinfo, fp);

    //Actually write the coefficents
    jpeg_write_coefficients(&dstinfo, dst_coef_arrays);

    qInfo() << "Output file wrote succesfully";

    //Write EXIF
    if (exif_flag == 2) {
        if (strcmp(input_file, exif_src) == 0) {
            jcopy_markers_execute(&srcinfo, &dstinfo);
        } else {
            //For standard compression EXIF data
            struct jpeg_decompress_struct einfo = cclt_get_markers(exif_src);
            jcopy_markers_execute(&einfo, &dstinfo);
            jpeg_destroy_decompress(&einfo);
        }
    }

    qInfo() << "EXIF copied, if any";

    //Finish and free
    jpeg_finish_compress(&dstinfo);
    jpeg_destroy_compress(&dstinfo);
    (void) jpeg_finish_decompress(&srcinfo);
    jpeg_destroy_decompress(&srcinfo);

    //Close the output file
    fclose(fp);

    return 0;
}
