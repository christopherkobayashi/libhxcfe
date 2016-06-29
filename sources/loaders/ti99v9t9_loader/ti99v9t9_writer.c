/*
//
// Copyright (C) 2006-2014 Jean-François DEL NERO
//
// This file is part of the HxCFloppyEmulator library
//
// HxCFloppyEmulator may be used and distributed without restriction provided
// that this copyright statement is not removed from the file and that any
// derivative work contains the original copyright notice and the associated
// disclaimer.
//
// HxCFloppyEmulator is free software; you can redistribute it
// and/or modify  it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// HxCFloppyEmulator is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//   See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with HxCFloppyEmulator; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "types.h"

#include "internal_libhxcfe.h"
#include "tracks/track_generator.h"
#include "libhxcfe.h"

#include "ti99v9t9_loader.h"

#include "tracks/sector_extractor.h"

#include "libhxcadaptor.h"

int TI99V9T9_libWrite_DiskFile(HXCFE_IMGLDR* imgldr_ctx,HXCFE_FLOPPY * floppy,char * filename)
{
	int i,j,k;
	FILE * ti99v9t9file;
	int32_t nbsector,imagesize;

	int32_t numberofsector,numberofside,numberoftrack;
	int32_t density = ISOIBM_FM_ENCODING;;
	int file_offset;
	int32_t sectorsize = 256;
	unsigned char * diskimage;
	int error = 0;
	HXCFE_SECTORACCESS* ss;
	HXCFE_SECTCFG* sc;

	imgldr_ctx->hxcfe->hxc_printf(MSG_INFO_1,"Write TI99 V9T9 file %s...",filename);

	imagesize=hxcfe_getFloppySize(imgldr_ctx->hxcfe,floppy,&nbsector);

	imgldr_ctx->hxcfe->hxc_printf(MSG_INFO_1,"Disk size : %d Bytes %d Sectors",imagesize,nbsector);

	ss = hxcfe_initSectorAccess(imgldr_ctx->hxcfe, floppy);
	if (ss)
	{
		sc = hxcfe_searchSector(ss, 0, 0, 0, density);
		if (!sc)
		{
			density = ISOIBM_MFM_ENCODING;
			sc = hxcfe_searchSector(ss, 0, 0, 0, density);
			if (!sc)
			{
				imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR, "This disk is neither FM nor MFM.  Exiting.");
				return HXCFE_FILECORRUPTED;
			}
		}

		// sc->input_data should contain the disk geometry

		numberofside = sc->input_data[0x12];
		numberofsector = sc->input_data[0x0c];
		numberoftrack = sc->input_data[0x11];

		if ( (numberofside < 1) && (numberofside > 2))
		{
			imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR, "Image claims it has %i sides, which is clearly wrong.  Exiting.", numberofside);
			return HXCFE_FILECORRUPTED;
		}
 
		if ( (numberoftrack != 40) && (numberoftrack != 80))
		{
			imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR, "Image claims each side has %i tracks, which is clearly wrong.  Exiting.", numberoftrack);
			return HXCFE_FILECORRUPTED;
		}
 
		if ( (numberofsector != 9) && (numberofsector != 16) && (numberofsector != 18) && (numberofsector != 36))
		{
			imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR, "Image claims each track has %i sectors, which is clearly wrong.  Exiting.", numberofsector);
			return HXCFE_FILECORRUPTED;
		}

		if ( (numberofsector * numberoftrack * numberofside) != nbsector )
		{
			imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR, "Disk geometry %i/%i/%i does not match disk length of %i sectors.  Exiting.", numberofside, numberoftrack, numberofsector, nbsector);
			return HXCFE_FILECORRUPTED;
		}


 
		imgldr_ctx->hxcfe->hxc_printf(MSG_INFO_1, "Disk geometry is %i sides, %i tracks per side, %i sectors per track.", numberofside, numberoftrack, numberofsector);

		imagesize = numberofsector * numberoftrack * numberofside * sectorsize;
		diskimage = malloc(imagesize);
		if (!diskimage)
			return HXCFE_INTERNALERROR;
		memset(diskimage, 0xF6, imagesize);

		for(i=0;i<numberofside;i++)
		{
			for(j=0;j<numberoftrack;j++)
			{
				hxcfe_imgCallProgressCallback(imgldr_ctx, j + (i*numberoftrack),numberofside*numberoftrack);

				for(k=0;k<numberofsector;k++)
				{
					sc = hxcfe_searchSector(ss,j,i,k,density);
					if(sc)
					{
						if(sc->use_alternate_data_crc)
							imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR,"Warning : Bad Data CRC : T:%d H:%d S:%d Size :%dB",j,i,k,sc->sectorsize);

						if(sc->sectorsize == sectorsize)
						{
							if(i==0)
								file_offset=(j*numberofsector)*sectorsize + ( k * sectorsize );
							else
								file_offset=(  numberoftrack      *numberofsector*sectorsize) +
												(((numberoftrack-1)-j)*numberofsector*sectorsize) +
												( k * sectorsize );
							memcpy(&diskimage[file_offset], sc->input_data, sectorsize);
						} else {
							error++;
							imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR,"Bad Sector Size : T:%d H:%d S:%d Size :%dB, Should be %dB",j,i,k,sc->sectorsize,sectorsize);
						}

						hxcfe_freeSectorConfig(ss,sc);
					} else {
						imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR,"Sector not found : T:%d H:%d S:%d",j,i,k);
					}
				}
			}
		}

		if(!error)
		{
			ti99v9t9file=hxc_fopen(filename,"wb");
			if(ti99v9t9file)
			{
				fwrite(diskimage,imagesize,1,ti99v9t9file);
				hxc_fclose(ti99v9t9file);
			} else {
				free(diskimage);
				hxcfe_deinitSectorAccess(ss);
				return HXCFE_ACCESSERROR;
			}
		}
	}

	free(diskimage);
	hxcfe_deinitSectorAccess(ss);

	if(!error)
		return HXCFE_NOERROR;
	else
	{
		imgldr_ctx->hxcfe->hxc_printf(MSG_ERROR,"This disk have some errors !");
		return HXCFE_FILECORRUPTED;
	}
}
