/*
 *  powder.cpp
 *  cheetah
 *
 *  Created by Anton Barty on 23/11/11.
 *  Copyright 2011 CFEL. All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <hdf5.h>
#include <stdlib.h>

#include "detectorObject.h"
#include "cheetahGlobal.h"
#include "cheetahEvent.h"
#include "cheetahmodules.h"
#include "median.h"



/*
 *	Maintain running powder patterns
 */

void addToPowder(cEventData *eventData, cGlobal *global) {

  int hit = eventData->hit;
    
  DETECTOR_LOOP {
    if(global->powderSumAll || global->generateDarkcal || global->generateGaincal || global->detector[detID].useAutoHalopixel){
      addToPowder(eventData, global, 0, detID);
    }

    if(hit && global->powderSumHits){
      addToPowder(eventData, global, hit+1, detID);
    }
    
    if(!hit && global->powderSumBlanks){
      addToPowder(eventData, global, hit+1, detID);
    }
  } 
}

void addToPowder(cEventData *eventData, cGlobal *global, int powderClass, int detID){
	
  // Dereference common variable
  long	radial_nn = global->detector[detID].radial_nn;
  long	pix_nn = global->detector[detID].pix_nn;
  long	image_nn = global->detector[detID].image_nn;

  double  *buffer;
	
  // Raw data
  pthread_mutex_lock(&global->detector[detID].powderRaw_mutex[powderClass]);
  for(long i=0; i<pix_nn; i++) 
    global->detector[detID].powderRaw[powderClass][i] += eventData->detector[detID].raw_data[i];
  pthread_mutex_unlock(&global->detector[detID].powderRaw_mutex[powderClass]);			

  // Corrected data
  pthread_mutex_lock(&global->detector[detID].powderCorrected_mutex[powderClass]);
  if(!global->usePowderThresh) {
    for(long i=0; i<pix_nn; i++) 
      global->detector[detID].powderCorrected[powderClass][i] += eventData->detector[detID].corrected_data[i];
  }
  else {
    for(long i=0; i<pix_nn; i++) {
      if(eventData->detector[detID].corrected_data[i] > global->powderthresh)
	global->detector[detID].powderCorrected[powderClass][i] += eventData->detector[detID].corrected_data[i];
    }
  }
  pthread_mutex_unlock(&global->detector[detID].powderCorrected_mutex[powderClass]);			

  // Corrected data squared (for calculating variance)
  buffer = (double*) calloc(pix_nn, sizeof(double));
  if(!global->usePowderThresh) {
    for(long i=0; i<pix_nn; i++)
      buffer[i] = (eventData->detector[detID].corrected_data[i])*(eventData->detector[detID].corrected_data[i]);
  }
  else {
    for(long i=0; i<pix_nn; i++){
      if(eventData->detector[detID].corrected_data[i] > global->powderthresh)
	buffer[i] = (eventData->detector[detID].corrected_data[i])*(eventData->detector[detID].corrected_data[i]);
      else
	buffer[i] = 0;
    }	
  }
  pthread_mutex_lock(&global->detector[detID].powderCorrectedSquared_mutex[powderClass]);
  for(long i=0; i<pix_nn; i++){
    global->detector[detID].powderCorrectedSquared[powderClass][i] += buffer[i];
  }
  pthread_mutex_unlock(&global->detector[detID].powderCorrectedSquared_mutex[powderClass]);	
  free(buffer);
	
  // Assembled data
  pthread_mutex_lock(&global->detector[detID].powderAssembled_mutex[powderClass]);
  if(!global->usePowderThresh) {
    for(long i=0; i<image_nn; i++)
      global->detector[detID].powderAssembled[powderClass][i] += eventData->detector[detID].image[i];
  }
  else {
    for(long i=0; i<image_nn; i++){
      if(eventData->detector[detID].image[i] > global->powderthresh)
	global->detector[detID].powderAssembled[powderClass][i] += eventData->detector[detID].image[i];
    }
  }
  pthread_mutex_unlock(&global->detector[detID].powderAssembled_mutex[powderClass]);

  // Increment counters
  pthread_mutex_lock(&global->detector[detID].powderCorrected_mutex[powderClass]);
  global->detector[detID].nPowderFrames[powderClass] += 1;
  if(detID == 0)
    global->nPowderFrames[powderClass] += 1;
  pthread_mutex_unlock(&global->detector[detID].powderCorrected_mutex[powderClass]);			

}


/*
 *	Wrapper for saving all powder patterns for a detector
 */
void saveRunningSums(cGlobal *global, int detID) {

  //	Save powder patterns from different classes
  printf("Writing intermediate powder patterns to file\n");
  for(long powderType=0; powderType < global->nPowderClasses; powderType++) {
    if(powderType == 0 && global->powderSumBlanks)
      savePowderPattern(global, detID, powderType);
    else if (powderType > 0 && global->powderSumHits)
      savePowderPattern(global, detID, powderType);
  }
	
  // Compute and save darkcal
  if(global->generateDarkcal) {
    saveDarkcal(global, detID);
  }
	
  // Compute and save gain calibration
  if(global->generateGaincal) {
    saveGaincal(global, detID);
  }
}


void savePowderPattern(cGlobal *global, int detID, int powderType) {
	
  // Dereference common variables
  cPixelDetectorCommon     *detector = &(global->detector[detID]);
  long	radial_nn = detector->radial_nn;
  long	pix_nn = detector->pix_nn;
  long	image_nn = detector->image_nn;
 
  /*	
   *	Filename
   */
  char	filename[1024];
  char	filenamebase[1024];
  sprintf(filenamebase,"r%04u-detector%d-class%d", global->runNumber, detID, powderType);
  //sprintf(filenamebase,"r%04u-class%i-%06i", global->runNumber, powderType, global->nprocessedframes);
  sprintf(filename,"%s-sum.h5",filenamebase);
  printf("%s\n",filename);
	
  /*
   *	Calculate powder patterns
   */
  // Raw data
  //sprintf(filename,"%s-sumRaw.h5",filenamebase);
  double *bufferRaw = (double*) calloc(pix_nn, sizeof(double));
  pthread_mutex_lock(&detector->powderRaw_mutex[powderType]);
  memcpy(bufferRaw, detector->powderRaw[powderType], pix_nn*sizeof(double));
  pthread_mutex_unlock(&detector->powderRaw_mutex[powderType]);

  // Corrected data
  //sprintf(filename,"%s-sumCorrected.h5",filenamebase);
  double *bufferCorrected = (double*) calloc(pix_nn, sizeof(double));
  double *radialAverageCorrected = (double*) calloc(radial_nn, sizeof(double));
  double *radialAverageCorrectedCounter = (double*) calloc(radial_nn, sizeof(double));
  pthread_mutex_lock(&detector->powderCorrected_mutex[powderType]);
  memcpy(bufferCorrected, detector->powderCorrected[powderType], pix_nn*sizeof(double));
  pthread_mutex_unlock(&detector->powderCorrected_mutex[powderType]);
  calculateRadialAverage(bufferCorrected, radialAverageCorrected, radialAverageCorrectedCounter, global, detID);

  //writePowderData(filename, bufferRaw, detector->pix_nx, detector->pix_ny, radialAverage, radialAverageCounter, radial_nn, detector->nPowderFrames[powderType], H5T_NATIVE_DOUBLE);	
  //free(bufferRaw);
	
  // Assembled sum
  //sprintf(filename,"%s-sumAssembled.h5", filenamebase);
  double *bufferAssembled = (double*) calloc(image_nn, sizeof(double));
  pthread_mutex_lock(&detector->powderAssembled_mutex[powderType]);
  memcpy(bufferAssembled, detector->powderAssembled[powderType], image_nn*sizeof(double));
  pthread_mutex_unlock(&detector->powderAssembled_mutex[powderType]);
	
  //writePowderData(filename, bufferAssembled, detector->image_nx, detector->image_nx, radialAverage, radialAverageCounter, radial_nn, detector->nPowderFrames[powderType], H5T_NATIVE_DOUBLE);	
	
  // Data squared (for calculation of variance)
  //sprintf(filename,"%s-sumCorrectedSquared.h5",filenamebase);
  double *bufferCorrectedSquared = (double*) calloc(pix_nn, sizeof(double));
  double *radialAverageCorrectedSquared = (double*) calloc(radial_nn, sizeof(double));
  pthread_mutex_lock(&detector->powderCorrectedSquared_mutex[powderType]);
  memcpy(bufferCorrectedSquared, detector->powderCorrectedSquared[powderType], pix_nn*sizeof(double));
  pthread_mutex_unlock(&detector->powderCorrectedSquared_mutex[powderType]);
  calculateRadialAverage(bufferCorrectedSquared, radialAverageCorrectedSquared, radialAverageCorrectedCounter, global, detID);
  //writePowderData(filename, bufferCorrectedSquared, detector->pix_nx, detector->pix_ny, radialAverage, radialAverageCounter, radial_nn, detector->nPowderFrames[powderType], H5T_NATIVE_DOUBLE);	
	
  // Sigma (variance)
  //sprintf(filename,"%s-sumCorrectedSigma.h5",filenamebase);
  double *bufferCorrectedSigma = (double*) calloc(pix_nn, sizeof(double));
  double *radialAverageCorrectedSigma = (double*) calloc(radial_nn, sizeof(double));
  pthread_mutex_lock(&detector->powderCorrected_mutex[powderType]);
  pthread_mutex_lock(&detector->powderCorrectedSquared_mutex[powderType]);
  for(long i=0; i<pix_nn; i++){
    bufferCorrectedSigma[i] =
      sqrt( (bufferCorrectedSquared[i] - bufferCorrected[i]*bufferCorrected[i]) / detector->nPowderFrames[powderType] );
  }
  pthread_mutex_unlock(&detector->powderCorrected_mutex[powderType]);
  pthread_mutex_unlock(&detector->powderCorrectedSquared_mutex[powderType]);
  calculateRadialAverage(bufferCorrectedSigma, radialAverageCorrectedSigma, radialAverageCorrectedCounter, global, detID);
  //writePowderData(filename, bufferSigma, detector->pix_nx, detector->pix_ny, radialAverage, radialAverageCounter, radial_nn, detector->nPowderFrames[powderType], H5T_NATIVE_DOUBLE);	
	
  /*
   *	Mess of stuff for writing the compound HDF5 file
   */
  hid_t fh, gh, sh, dh;	/* File, group, dataspace and data handles */
  herr_t r;
  hsize_t size[2];
  hsize_t max_size[2];
	
  fh = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if ( fh < 0 ) {
    ERROR("Couldn't create HDF5 file: %s\n", filename);
  }
  gh = H5Gcreate(fh, "data", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if ( gh < 0 ) {
    ERROR("Couldn't create HDF5 group\n");
    H5Fclose(fh);
  }
	
  // Write image data in Raw layout
  size[0] = detector->pix_ny;
  size[1] = detector->pix_nx;
  sh = H5Screate_simple(2, size, NULL);
  //H5Sget_simple_extent_dims(sh, size, max_size);
	
  dh = H5Dcreate(gh, "rawdata", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, bufferRaw);
  H5Dclose(dh);

  dh = H5Dcreate(gh, "correcteddata", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, bufferCorrected);
  H5Dclose(dh);
	
  //H5Sget_simple_extent_dims(sh, size, size);
  dh = H5Dcreate(gh, "correcteddatasquared", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, bufferCorrectedSquared);
  H5Dclose(dh);
	
  //H5Sget_simple_extent_dims(sh, size, size);
  dh = H5Dcreate(gh, "correcteddatasigma", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, bufferCorrectedSigma);
  H5Dclose(dh);
  H5Sclose(sh);

	
  // Write assembled image data
  size[0] = detector->image_nx;
  size[1] = detector->image_nx;
  sh = H5Screate_simple(2, size, NULL);
	
  //H5Sget_simple_extent_dims(sh, size, max_size);
  dh = H5Dcreate(gh, "assembleddata", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, bufferAssembled);
  H5Dclose(dh);
  H5Sclose(sh);

  H5Lcreate_soft( "/data/assembleddata", fh, "/data/data",0,0);


  // Save radial averages
  size[0] = radial_nn;
  max_size[0] = radial_nn;
  sh = H5Screate_simple(1, size, NULL);
  //H5Sget_simple_extent_dims(sh, size, max_size);
	
  dh = H5Dcreate(gh, "radialAverageCorrected", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, radialAverageCorrected);
  H5Dclose(dh);

  dh = H5Dcreate(gh, "radialAverageCorrectedSquared", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, radialAverageCorrectedSquared);
  H5Dclose(dh);

  dh = H5Dcreate(gh, "radialAverageCorrectedSigma", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, radialAverageCorrectedSigma);
  H5Dclose(dh);

  dh = H5Dcreate(gh, "radialAverageCorrectedCounter", H5T_NATIVE_DOUBLE, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, radialAverageCorrectedCounter);
  H5Dclose(dh);	
  H5Sclose(sh);
	
	
  // Save frame count
  size[0] = 1;
  sh = H5Screate_simple(1, size, NULL );
  //H5Sget_simple_extent_dims(sh, size, size);
  //sh = H5Screate(H5S_SCALAR);
	
  dh = H5Dcreate(gh, "nframes", H5T_NATIVE_LONG, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (dh < 0) ERROR("Could not create dataset.\n");
  H5Dwrite(dh, H5T_NATIVE_LONG, H5S_ALL, H5S_ALL, H5P_DEFAULT, &detector->nPowderFrames[powderType] );
  H5Dclose(dh);
  H5Sclose(sh);
	
	
  // Clean up stale HDF5 links
  H5Gclose(gh);
  int n_ids;
  hid_t ids[256];
  n_ids = H5Fget_obj_ids(fh, H5F_OBJ_ALL, 256, ids);
  for ( int i=0; i<n_ids; i++ ) {
    hid_t id;
    H5I_type_t type;
    id = ids[i];
    type = H5Iget_type(id);
    if ( type == H5I_GROUP ) H5Gclose(id);
    if ( type == H5I_DATASET ) H5Dclose(id);
    if ( type == H5I_DATATYPE ) H5Tclose(id);
    if ( type == H5I_DATASPACE ) H5Sclose(id);
    if ( type == H5I_ATTR ) H5Aclose(id);
  }
  H5Fclose(fh);

	

  /*
   *	Clean up stuff
   */
  free(bufferRaw);
  free(bufferCorrected);
  free(bufferAssembled);
  free(bufferCorrectedSquared);
  free(bufferCorrectedSigma);
  free(radialAverageCorrected);
  free(radialAverageCorrectedSquared);
  free(radialAverageCorrectedSigma);
  free(radialAverageCorrectedCounter);
  fflush(global->powderlogfp[powderType]);

}



/*
 *	Compute and save dark calibration
 */
void saveDarkcal(cGlobal *global, int detID) {
	
  // Dereference common variables
  cPixelDetectorCommon     *detector = &(global->detector[detID]);
  long	pix_nn = detector->pix_nn;
  char	filename[1024];
	
  printf("Processing darkcal\n");
  sprintf(filename,"r%04u-%s-darkcal.h5",global->runNumber, detector->detectorName);
  float *buffer = (float*) calloc(pix_nn, sizeof(float));
  pthread_mutex_lock(&detector->powderCorrected_mutex[0]);
  for(long i=0; i<pix_nn; i++)
    buffer[i] = detector->powderCorrected[0][i]/detector->nPowderFrames[0];
  pthread_mutex_unlock(&detector->powderCorrected_mutex[0]);
  printf("Saving darkcal to file: %s\n", filename);
  writeSimpleHDF5(filename, buffer, detector->pix_nx, detector->pix_ny, H5T_NATIVE_FLOAT);	
  free(buffer);
}

/*
 *	Compute and save gain calibration
 */
void saveGaincal(cGlobal *global, int detID) {
	
  // Dereference common variables
  cPixelDetectorCommon     *detector = &(global->detector[detID]);
  long	pix_nn = detector->pix_nn;
  char	filename[1024];
	
  printf("Processing gaincal\n");
  sprintf(filename,"r%04u-%s-gaincal.h5",global->runNumber, detector->detectorName);
  // Calculate average intensity per frame
  pthread_mutex_lock(&detector->powderCorrected_mutex[0]);
  double *buffer = (double*) calloc(pix_nn, sizeof(double));
  for(long i=0; i<pix_nn; i++)
    buffer[i] = (detector->powderCorrected[0][i]/detector->nPowderFrames[0]);
  pthread_mutex_unlock(&detector->powderCorrected_mutex[0]);
	
  // Find median value (this value will become gain=1)
  float *buffer2 = (float*) calloc(pix_nn, sizeof(float));
  for(long i=0; i<pix_nn; i++) {
    buffer2[i] = buffer[i];
  }
  float	dc;
  dc = kth_smallest(buffer2, pix_nn, lrint(0.5*pix_nn));
  printf("offset=%f\n",dc);
  free(buffer2);
  if(dc <= 0){
    printf("Error calculating gain, offset = %f\n",dc);
    return;
  }
  // gain=1 for a median value pixel, and is bounded between a gain of 0.1 and 10
  for(long i=0; i<pix_nn; i++) {
    buffer[i] /= (double) dc;
    if(buffer[i] < 0.1 || buffer[i] > 10)
      buffer[i]=0;
  }
  printf("Saving gaincal to file: %s\n", filename);
  writeSimpleHDF5(filename, buffer, detector->pix_nx, detector->pix_ny, H5T_NATIVE_DOUBLE);	
  free(buffer);	
}


/*
 *	Write a single powder pattern to file
 */
void writePowderData(char *filename, void *data, int width, int height, void *radialAverageCorrected, void *radialAverageCorrectedCounter, long radial_nn, long nFrames, int type) 
{
  hid_t fh, gh, sh, dh;	/* File, group, dataspace and data handles */
  herr_t r;
  hsize_t size[2];
  hsize_t max_size[2];
	
  fh = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if ( fh < 0 ) {
    ERROR("Couldn't create file: %s\n", filename);
  }
	
  gh = H5Gcreate(fh, "data", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if ( gh < 0 ) {
    ERROR("Couldn't create group\n");
    H5Fclose(fh);
  }
	
  // Write image data
  size[0] = height;
  size[1] = width;
  max_size[0] = height;
  max_size[1] = width;
  sh = H5Screate_simple(2, size, NULL);
  dh = H5Dcreate(gh, "data", type, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
	
  H5Sget_simple_extent_dims(sh, size, max_size);
  H5Dwrite(dh, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
  H5Dclose(dh);
	
	
  // Save radial averages
  size[0] = radial_nn;
  sh = H5Screate_simple(1, size, NULL);
	
  dh = H5Dcreate(gh, "radialAverageCorrected", type, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, radialAverageCorrected);
  H5Dclose(dh);
	
  dh = H5Dcreate(gh, "radialAverageCorrectedCounter", type, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  H5Dwrite(dh, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, radialAverageCorrectedCounter);
  H5Dclose(dh);	
  H5Sclose(sh);
	
	
  // Save frame count
  size[0] = 1;
  sh = H5Screate_simple(1, size, NULL );
  //sh = H5Screate(H5S_SCALAR);
	
  dh = H5Dcreate(gh, "nframes", H5T_NATIVE_LONG, sh, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  H5Dwrite(dh, H5T_NATIVE_LONG, H5S_ALL, H5S_ALL, H5P_DEFAULT, &nFrames );
  H5Dclose(dh);
  H5Sclose(sh);
	
	
  // Close group
  H5Gclose(gh);
	
	
  // Clean up stale HDF5 links
  int n_ids;
  hid_t ids[256];
  n_ids = H5Fget_obj_ids(fh, H5F_OBJ_ALL, 256, ids);
  for ( int i=0; i<n_ids; i++ ) {
    hid_t id;
    H5I_type_t type;
    id = ids[i];
    type = H5Iget_type(id);
    if ( type == H5I_GROUP ) H5Gclose(id);
    if ( type == H5I_DATASET ) H5Dclose(id);
    if ( type == H5I_DATATYPE ) H5Tclose(id);
    if ( type == H5I_DATASPACE ) H5Sclose(id);
    if ( type == H5I_ATTR ) H5Aclose(id);
  }
	
	
  H5Fclose(fh);
}

