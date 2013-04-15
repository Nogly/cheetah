/*
 *  worker.cpp
 *  cheetah
 *
 *  Created by Anton Barty on 6/2/11.
 *  Copyright 2011 CFEL. All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <hdf5.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "cheetah.h"
#include "cheetahmodules.h"
#include "median.h"

#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>

/*
 *	Worker thread function for processing each cspad data frame
 */
void *worker(void *threadarg) {

  /*
   *	Turn threadarg into a more useful form
   */
  cGlobal			*global;
  cEventData		*eventData;
  eventData = (cEventData*) threadarg;
  global = eventData->pGlobal;
  int	hit = 0;
	
  std::vector<int> myvector;
  std::stringstream sstm;
  std::string result;
  std::ofstream outFlu;
  std::ios_base::openmode mode;
  std::stringstream sstm1;
  std::ofstream outHit;

  /*
   * Nasty fudge for evr41 (i.e. "optical pump laser is on") signal when only 
   * Acqiris data (i.e. temporal profile of the laser diode signal) is available...
   * Hopefully this never happens again... 
   */
  if ( global->fudgeevr41 == 1 ) {
    evr41fudge(eventData,global);	
  }
	

	
  /*
   *	Create a unique name for this event
   */
  nameEvent(eventData, global);
	
  /*
   *    Calculate GMD running average
   */
  updateAvgGMD(eventData,global);

  DETECTOR_LOOP {
    /*
     *	Copy pixelmask_shared into pixelmask for this event
     */
    for(long i=0;i<global->detector[detID].pix_nn;i++){
      eventData->detector[detID].pixelmask[i] = global->detector[detID].pixelmask_shared[i];
    }
    /*
     *	Copy raw detector data into corrected array as starting point for corrections
     */
    for(long i=0;i<global->detector[detID].pix_nn;i++){
      eventData->detector[detID].corrected_data[i] = eventData->detector[detID].raw_data[i];
    }
    if(global->detector[detID].useAutoHalopixel) {
    /*
     *	Copy raw detector data into corrected array as starting point for corrections
     */
      memcpy(eventData->detector[detID].runningCorrelationsPixGMD,global->detector[detID].runningCorrelationsPixGMD,global->detector[detID].pix_nn*sizeof(float));
    }
    
  }
	
  /*
   * Check for saturated pixels before applying any other corrections
   */
  checkSaturatedPixels(eventData, global);


	
  /*
   *	Subtract darkcal image (static electronic offsets)
   */
  subtractDarkcal(eventData, global);

  /*
   *	Subtract common mode offsets (electronic offsets)
   *	cmModule = 1
   */
  cspadModuleSubtract(eventData, global);
  cspadSubtractUnbondedPixels(eventData, global);
  cspadSubtractBehindWires(eventData, global);

	
  /*
   *	Apply gain correction
   */
  applyGainCorrection(eventData, global);

	
  /*
   *	Apply bad pixel map
   */
  applyBadPixelMask(eventData, global);
	
	
  /* 
   *	Keep memory of data with only detector artefacts subtracted 
   *	(possibly needed later)
   */
  DETECTOR_LOOP {
    memcpy(eventData->detector[detID].detector_corrected_data, eventData->detector[detID].corrected_data, global->detector[detID].pix_nn*sizeof(float));
		
    for(long i=0;i<global->detector[detID].pix_nn;i++){
      eventData->detector[detID].corrected_data_int16[i] = (int16_t) lrint(eventData->detector[detID].corrected_data[i]);
    }
  }

  updateHaloPixelBuffers(eventData, global);
       
  /*
   *	Subtract persistent photon background
   */
  subtractPersistentBackground(eventData, global);

    	

  /*
   *	Local background subtraction
   */
  subtractLocalBackground(eventData, global);
			

  /*
   *	Subtract residual common mode offsets (cmModule=2)
   */
  cspadModuleSubtract2(eventData, global);

  /*
   *	Apply bad pixels
   */
  applyBadPixelMask(eventData, global);
	
  /*
   *	Identify and kill hot pixels
   */
  identifyHotPixels(eventData, global);	
  calculateHotPixelMask(global);
  applyHotPixelMask(eventData,global);

  updateDatarate(eventData,global);

  /*
   *	Skip first set of frames to build up running estimate of background...
   */
  DETECTOR_LOOP {
    if (eventData->threadNum < global->detector[detID].startFrames || 
	(global->detector[detID].useSubtractPersistentBackground && global->detector[detID].bgCounter < global->detector[detID].bgMemory) || 
	(global->detector[detID].useAutoHotpixel && global->detector[detID].hotpixCounter < global->detector[detID].hotpixRecalc) ) {
      updateBackgroundBuffer(eventData, global, detID); 
		    
      printf("r%04u:%li (%3.1fHz): Digesting initial frames\n", global->runNumber, eventData->threadNum,global->datarateWorker);
      goto cleanup;
    }
  }
    

  /*
   *  Fix pnCCD errors:
   *      pnCCD offset correction (read out artifacts prominent in lines with high signal)
   *      pnCCD wiring error (shift in one set of rows relative to another - and yes, it's a wiring error).
   *  (these corrections will be automatically skipped for any non-pnCCD detector)
   */
  pnccdOffsetCorrection(eventData, global);
  pnccdFixWiringError(eventData, global);
    

  /*
   *	Hitfinding
   */
  if(global->hitfinder){
    hit = hitfinder(eventData, global);
    eventData->hit = hit;
  }
	
  /*
   *	Identify halo pixels
   */
  //identifyHaloPixels(eventData, global);	
  //calculateHaloPixelMask(global);
  // CHANGE!!!
  //calculatePixelGMDCorrelations(eventData,global);
  calculatePixelGMDCorrelations(global);
	
  /*
   *	Update running backround estimate based on non-hits
   */
  updateBackgroundBuffer(eventData, global, hit); 
	
	
  /*
   *	Revert to detector-corrections-only data if we don't want to export data with photon background subtracted
   */
  DETECTOR_LOOP {
    if(global->detector[detID].saveDetectorCorrectedOnly) 
      memcpy(eventData->detector[detID].corrected_data, eventData->detector[detID].detector_corrected_data, global->detector[detID].pix_nn*sizeof(float));
  }
	
	
  /*
   *	If using detector raw, do it here
   */
  DETECTOR_LOOP {
    if(global->detector[detID].saveDetectorRaw) 
      for(long i=0;i<global->detector[detID].pix_nn;i++)
	eventData->detector[detID].corrected_data[i] = eventData->detector[detID].raw_data[i];
  }
	
	
  /*
   *	Keep int16 copy of corrected data (needed for saving images)
   */
  DETECTOR_LOOP {
    for(long i=0;i<global->detector[detID].pix_nn;i++){
      eventData->detector[detID].corrected_data_int16[i] = (int16_t) lrint(eventData->detector[detID].corrected_data[i]);
    }
  }

  /*
   *	Write cspad to file in 1D
   */
  /*
  //	if (eventData->threadNum >= 0) {
  std::cout << "Write cspad to file.. single threaded" << std::endl;
  DETECTOR_LOOP {
  for(long i=0;i<global->detector[detID].pix_nn;i++){
  myvector.push_back( (int16_t) lrint(eventData->detector[detID].corrected_data[i]) );
  }
  }
  // Write out files
  sstm << "r0" << global->runNumber << "_cspad_corrected1D_" << eventData->frameNumber;
  result = sstm.str();
  //outFlu.open(result.c_str(), mode = std::ios_base::app); // output cspad for all shots
  outFlu.open(result.c_str()); // output cspad for all shots
  //std::cout << myvector.size() << std::endl;
  //std::cout << global->nDetectors << std::endl;
  for (unsigned i = 0; i < myvector.size(); i++ ) {
  outFlu << myvector[i] << " ";
  }
  outFlu << std::endl;
  outFlu.close();

  sstm1 << "r0" << global->runNumber << "_cspad_corrected1D_target_" << eventData->frameNumber;
  result = sstm1.str();
  outHit.open(result.c_str()); // output cspad for all shots
  outHit << eventData->hit;
  outHit << std::endl;
  outHit.close();

  //	}
  */

  /*
   *	Assemble quadrants into a 'realistic' 2D image
   */
  if(global->assemble2DImage) {
    assemble2Dimage(eventData, global);
  }
  if(global->assemble2DMask) {
    assemble2Dmask(eventData, global);
  }

  /*
   *	Calculate radial average
   *  Maintain radial average stack
   */
  calculateRadialAverage(eventData, global); 
  addToRadialAverageStack(eventData, global);
	
  /*
   *	Maintain a running sum of data (powder patterns)
   */
  addToPowder(eventData, global);

  /*
   * calculate the one dimesional beam spectrum
   */
  integrateSpectrum(eventData, global);
  integrateRunSpectrum(eventData, global);

  /*
   *	If this is a hit, write out to our favourite HDF5 format
   */
 save:
  if((hit && global->savehits) || ((global->hdf5dump > 0) && ((eventData->frameNumber % global->hdf5dump) == 0))){
    if(global->saveCXI==1){
      pthread_mutex_lock(&global->saveCXI_mutex);
      writeCXI(eventData, global);
      pthread_mutex_unlock(&global->saveCXI_mutex);
    }
    else {
      writeHDF5(eventData, global);
      printf("r%04u:%li (%2.1lf Hz): Writing to: %s (npeaks=%i)\n",global->runNumber, eventData->threadNum,global->datarateWorker, eventData->eventname, eventData->nPeaks);
    }
  }
  else {
    printf("r%04u:%li (%2.1lf Hz): Processed (npeaks=%i)\n", global->runNumber,eventData->threadNum,global->datarateWorker, eventData->nPeaks);
  }

  /*
   *	If this is a hit, write out peak info to peak list file
   */
  if(hit && global->savePeakInfo) {
    writePeakFile(eventData, global);
  }

  /*
   *	Write out information on each frame to a log file
   */
  pthread_mutex_lock(&global->framefp_mutex);
  fprintf(global->framefp, "%s, %li, %li, %i, %g, %g, %g, %g, %g, %i, %d, %d, %g, %g, %g, %d, %g, %d\n", eventData->eventname, eventData->frameNumber, eventData->threadNum, eventData->hit, eventData->photonEnergyeV, eventData->wavelengthA, eventData->gmd1, eventData->gmd2, eventData->detector[0].detectorZ, eventData->energySpectrumExist, eventData->nPeaks, eventData->peakNpix, eventData->peakTotal, eventData->peakResolution, eventData->peakDensity, eventData->laserEventCodeOn, eventData->laserDelay, eventData->samplePumped);
  pthread_mutex_unlock(&global->framefp_mutex);

  // Keep track of what has gone into each image class
  pthread_mutex_lock(&global->powderfp_mutex);
  fprintf(global->powderlogfp[hit], "%s, %li, %li, %g, %g, %g, %g, %g, %i, %d, %d, %g, %g, %g, %d, %g\n", eventData->eventname, eventData->frameNumber, eventData->threadNum, eventData->photonEnergyeV, eventData->wavelengthA, eventData->detector[0].detectorZ, eventData->gmd1, eventData->gmd2, eventData->energySpectrumExist,  eventData->nPeaks, eventData->peakNpix, eventData->peakTotal, eventData->peakResolution, eventData->peakDensity, eventData->laserEventCodeOn, eventData->laserDelay);
  pthread_mutex_unlock(&global->powderfp_mutex);
	
  /*
   *	Cleanup and exit
   */
 cleanup:
  // Decrement thread pool counter by one
  pthread_mutex_lock(&global->nActiveThreads_mutex);
  global->nActiveThreads -= 1;
  pthread_mutex_unlock(&global->nActiveThreads_mutex);
    
  // Free memory only if running multi-threaded
  if(eventData->useThreads == 1) {
    cheetahDestroyEvent(eventData);
    pthread_exit(NULL);
  }
  else {
    return(NULL);
  }
}


/*
 * Nasty little bit of code that aims to toggle the evr41 signal based on the Acqiris
 * signal.  Very simple: scan along the Acqiris trace (starting from the ini keyword 
 * hitfinderTOFMinSample and ending at hitfinderTOFMaxSample) and check that there is 
 * at least one sample above the threshold set by the hitfinderTOFThresh keyword. Oh,
 * and don't forget to specify the Acqiris channel with:
 * tofChannel=0
 * And you'll need to do this as well:
 * fudgeevr41=1
 * tofName=CxiSc1
 *
 * Agreed - doesn't sound very robust.  As far as I can tell at the moment, only a single
 * sample rises above threshold when the laser trigger is on... maybe bad sampling interval?
 * Or, I could be wrong...
 * 
 * -Rick  
 */
void evr41fudge(cEventData *t, cGlobal *g){
	
	if ( g->TOFPresent == 0 ) {
		//printf("Acqiris not present; can't fudge EVR41...\n");
		return;
	}
 
	int nCh = g->AcqNumChannels;
	int nSamp = g->AcqNumSamples;
	double * Vtof = t->TOFVoltage;
	int i;
	double Vtot = 0;
	double Vmax = 0;
	int tCounts = 0;
	for(i=g->hitfinderTOFMinSample; i<g->hitfinderTOFMaxSample; i++){
		Vtot += Vtof[i];
		if ( Vtof[i] > Vmax ) Vmax = Vtof[i];
		if ( Vtof[i] >= g->hitfinderTOFThresh ) tCounts++;
	}
	

	bool acqLaserOn = false;
	if ( tCounts >= 1 ) {
		acqLaserOn = true;
	}
	//if ( acqLaserOn ) printf("acqLaserOn = true\n"); else printf("acqLaserOn = false\n");
	//if ( t->laserEventCodeOn ) printf("laserEventCodeOn = true\n"); else printf("laserEventCodeOn = false\n");
	if ( acqLaserOn != t->laserEventCodeOn ) {
		if ( acqLaserOn ) {
			printf("MESSAGE: Acqiris and evr41 disagree.  We trust acqiris (set evr41 = 1 )\n");
		} else {
			printf("MESSAGE: Acqiris and evr41 disagree.  We trust acqiris (set evr41 = 0 )\n");
		}
		t->laserEventCodeOn = acqLaserOn;
	}
}


double difftime_timeval(timeval t1, timeval t2)
{
  return ((t1.tv_sec - t2.tv_sec) + (t1.tv_usec - t2.tv_usec) / 1000000.0 );
}

void updateDatarate(cEventData *eventData, cGlobal *global){

  timeval timevalNow;
  double datarateNow;
  double mem = global->datarateWorkerMemory;
  double dtNew,dtNow;
  gettimeofday(&timevalNow, NULL);
  
  pthread_mutex_lock(&global->datarateWorker_mutex);
  if (timercmp(&timevalNow,&global->datarateWorkerTimevalLast,!=)){
    dtNow = difftime_timeval(timevalNow,global->datarateWorkerTimevalLast) / (1+global->datarateWorkerSkipCounter);
    dtNew = 1/global->datarateWorker * mem + dtNow * (1-mem);
    global->datarateWorker = 1/dtNew;
    global->datarateWorkerTimevalLast = timevalNow;
    global->datarateWorkerSkipCounter = 0;
  }else{
    global->datarateWorkerSkipCounter += 1;
  }
  pthread_mutex_unlock(&global->datarateWorker_mutex);

  
}

void updateAvgGMD(cEventData *eventData, cGlobal *global){

  /*
   *	Remember GMD values
   */
  float	gmd1,gmd2;
  float mem =  global->avgGMDMemory;
  gmd1 = (eventData->gmd11+eventData->gmd12)/2.;
  gmd2 = (eventData->gmd21+eventData->gmd22)/2.;
  pthread_mutex_lock(&global->gmd_mutex);
  global->avgGMD1 = global->avgGMD1 * mem + gmd1 * (1-mem);
  global->avgGMD2 = global->avgGMD2 * mem + gmd2 * (1-mem);
  global->avgSqGMD1 = global->avgSqGMD1 * mem + gmd1 * gmd1 * (1-mem);
  global->avgSqGMD2 = global->avgSqGMD2 * mem + gmd2 * gmd2 * (1-mem);
  //printf("GMD1 = %f mJ [(%f +/- %f) mJ]\n",gmd1,global->avgGMD1,sqrt(global->avgSqGMD1-global->avgGMD1*global->avgGMD1));
  //printf("GMD2 = %f mJ [(%f +/- %f) mJ]\n",gmd2,global->avgGMD2,sqrt(global->avgSqGMD2-global->avgGMD2*global->avgGMD2));
  pthread_mutex_unlock(&global->gmd_mutex);  
}
