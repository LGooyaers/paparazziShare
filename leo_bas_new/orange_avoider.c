/*
 * Copyright (C) Roland Meertens
 *
 * This file is part of paparazzi
 *
 */
/**
 * @file "modules/orange_avoider/orange_avoider.c"
 * @author Roland Meertens
 * Example on how to use the colours detected to avoid orange pole in the cyberzoo
 */

#include "modules/orange_avoider/orange_avoider.h"
#include "modules/computer_vision/colorfilter.h"
#include "firmwares/rotorcraft/navigation.h"
#include "generated/flight_plan.h"
#include "generated/airframe.h"
#include "state.h"
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>


#define ORANGE_AVOIDER_VERBOSE TRUE

#define PRINT(string,...) fprintf(stderr, "[orange_avoider->%s()] " string,__FUNCTION__ , ##__VA_ARGS__)
#if ORANGE_AVOIDER_VERBOSE
#define VERBOSE_PRINT PRINT
#else
#define VERBOSE_PRINT(...)
#endif

#ifndef ORANGE_AVOIDER_LUM_MIN
#define ORANGE_AVOIDER_LUM_MIN 41
#endif

#ifndef ORANGE_AVOIDER_LUM_MAX
#define ORANGE_AVOIDER_LUM_MAX 183
#endif

#ifndef ORANGE_AVOIDER_CB_MIN
#define ORANGE_AVOIDER_CB_MIN 53
#endif

#ifndef ORANGE_AVOIDER_CB_MAX
#define ORANGE_AVOIDER_CB_MAX 121
#endif

#ifndef ORANGE_AVOIDER_CR_MIN
#define ORANGE_AVOIDER_CR_MIN 134
#endif

#ifndef ORANGE_AVOIDER_CR_MAX
#define ORANGE_AVOIDER_CR_MAX 249
#endif

#ifndef GRID_ROWS
#define GRID_ROWS 3
#endif

#ifndef GRID_COLUMNS
#define GRID_COLUMNS 5
#endif

#ifndef GRID_WEIGHTS
#define GRID_WEIGHTS {1, 2, 3}
#endif

#ifndef GRID_THRESHOLD
#define GRID_THRESHOLD 5000
#endif

uint8_t safeToGoForwards        = false;
// int tresholdColorCount          = 0.05 * 124800; // 520 x 240 = 124.800 total pixels
float incrementForAvoidance;
uint16_t trajectoryConfidence   = 1;
float maxDistance               = 2.25;


#define safetyThreshold 3
#define averageThreshold 2
#define V1 0.0
#define V2 0.01
#define V3 0.05
#define increment 10.0

float incrementForAvoidance;
uint8_t V;
uint8_t currentWp;
// uint8_t vision_vector[5];
uint8_t obstaclesPresent[5];
uint8_t midpoint = (GRID_COLUMNS-1)/2; //midpoint of the vision output vector



/*
 * Initialisation function, setting the colour filter, random seed and incrementForAvoidance
 */
void orange_avoider_init()
{
  // Initialise the variables of the colorfilter to accept orange
  color_lum_min = ORANGE_AVOIDER_LUM_MIN;
  color_lum_max = ORANGE_AVOIDER_LUM_MAX;
  color_cb_min  = ORANGE_AVOIDER_CB_MIN;
  color_cb_max  = ORANGE_AVOIDER_CB_MAX;
  color_cr_min  = ORANGE_AVOIDER_CR_MIN;
  color_cr_max  = ORANGE_AVOIDER_CR_MAX;

  numRows       = GRID_ROWS;
  numCols       = GRID_COLUMNS;
  numCells		  = GRID_CELLS;

  threshold_cell          = GRID_THRESHOLD;

  // Initialise random values
  srand(time(NULL));
  //chooseRandomIncrementAvoidance();
  chooseIncrementAvoidance();
}

/*
 * Function that checks it is safe to move forwards, and then moves a waypoint forward or changes the heading
 */
void orange_avoider_periodic()
{
//  VERBOSE_PRINT("Color counts: \n [%d, %d, %d, %d, %d] \n [%d, %d, %d, %d, %d] \n [%d, %d, %d, %d, %d] \n", color_count_cells[2], color_count_cells[5], color_count_cells[8], color_count_cells[11], color_count_cells[14]
//																										 , color_count_cells[1], color_count_cells[4], color_count_cells[7], color_count_cells[10], color_count_cells[13]
//																										 , color_count_cells[0], color_count_cells[3], color_count_cells[6], color_count_cells[9], color_count_cells[12]);
  VERBOSE_PRINT("vision vector[%d, %d, %d, %d, %d]\n", vision_vector[1],vision_vector[2],vision_vector[3],vision_vector[4],vision_vector[5]);
  // Check the amount of green. If this is below a threshold
  // you want to turn a certain amount of degrees
  safeToGoForwards = (
		  vision_vector[midpoint] < safetyThreshold &&
		  vision_vector[midpoint-1]< safetyThreshold &&
		  vision_vector[midpoint+1]< safetyThreshold );
  VERBOSE_PRINT("safe to go forward: %d \n", safeToGoForwards);
  float moveDistance = fmin(maxDistance, 0.05 * trajectoryConfidence);
  if (safeToGoForwards) {
    moveWaypointForward(WP_GOAL, moveDistance);
    moveWaypointForward(WP_TRAJECTORY, 1.25 * moveDistance);
    nav_set_heading_towards_waypoint(WP_GOAL);
    //chooseRandomIncrementAvoidance();
    chooseIncrementAvoidance();
    trajectoryConfidence += 2;
  } else {
    waypoint_set_here_2d(WP_GOAL);
    waypoint_set_here_2d(WP_TRAJECTORY);
    increase_nav_heading(&nav_heading, incrementForAvoidance);
    if (trajectoryConfidence > 5) {
      trajectoryConfidence -= 4;
    } else {
      trajectoryConfidence = 1;
    }
  }
  return;
}

/*
 * Increases the NAV heading. Assumes heading is an INT32_ANGLE. It is bound in this function.
 */
uint8_t increase_nav_heading(int32_t *heading, float incrementDegrees)
{
  struct Int32Eulers *eulerAngles   = stateGetNedToBodyEulers_i();
  int32_t newHeading = eulerAngles->psi + ANGLE_BFP_OF_REAL(RadOfDeg(incrementDegrees));
  // Check if your turn made it go out of bounds...
  INT32_ANGLE_NORMALIZE(newHeading); // HEADING HAS INT32_ANGLE_FRAC....
  *heading = newHeading;
  VERBOSE_PRINT("Increasing heading to %f\n", DegOfRad(ANGLE_FLOAT_OF_BFP(*heading)));
  return false;
}

/*
 * Calculates coordinates of a distance of 'distanceMeters' forward w.r.t. current position and heading
 */
static uint8_t calculateForwards(struct EnuCoor_i *new_coor, float distanceMeters)
{
  struct EnuCoor_i *pos             = stateGetPositionEnu_i(); // Get your current position
  struct Int32Eulers *eulerAngles   = stateGetNedToBodyEulers_i();
  // Calculate the sine and cosine of the heading the drone is keeping
  float sin_heading                 = sinf(ANGLE_FLOAT_OF_BFP(eulerAngles->psi));
  float cos_heading                 = cosf(ANGLE_FLOAT_OF_BFP(eulerAngles->psi));
  // Now determine where to place the waypoint you want to go to
  new_coor->x                       = pos->x + POS_BFP_OF_REAL(sin_heading * (distanceMeters));
  new_coor->y                       = pos->y + POS_BFP_OF_REAL(cos_heading * (distanceMeters));
//  VERBOSE_PRINT("Calculated %f m forward position. x: %f  y: %f based on pos(%f, %f) and heading(%f)\n", distanceMeters,
//                POS_FLOAT_OF_BFP(new_coor->x), POS_FLOAT_OF_BFP(new_coor->y), POS_FLOAT_OF_BFP(pos->x), POS_FLOAT_OF_BFP(pos->y),
//                DegOfRad(ANGLE_FLOAT_OF_BFP(eulerAngles->psi)) );
  return false;
}

/*
 * Sets waypoint 'waypoint' to the coordinates of 'new_coor'
 */
uint8_t moveWaypoint(uint8_t waypoint, struct EnuCoor_i *new_coor)
{
//  VERBOSE_PRINT("Moving waypoint %d to x:%f y:%f\n", waypoint, POS_FLOAT_OF_BFP(new_coor->x),
//                POS_FLOAT_OF_BFP(new_coor->y));
  waypoint_set_xy_i(waypoint, new_coor->x, new_coor->y);
  return false;
}

/*
 * Calculates coordinates of distance forward and sets waypoint 'waypoint' to those coordinates
 */
uint8_t moveWaypointForward(uint8_t waypoint, float distanceMeters)
{
  struct EnuCoor_i new_coor;
  calculateForwards(&new_coor, distanceMeters);
  moveWaypoint(waypoint, &new_coor);
  return false;
}

/*
 * Sets the variable 'incrementForAvoidance' randomly positive/negative
 */
uint8_t chooseRandomIncrementAvoidance()
{
  // Randomly choose CW or CCW avoiding direction
  int r = rand() % 2;
  if (r == 0) {
    incrementForAvoidance = 10.0;
    VERBOSE_PRINT("Set avoidance increment to: %f\n", incrementForAvoidance);
  } else {
    incrementForAvoidance = -10.0;
    VERBOSE_PRINT("Set avoidance increment to: %f\n", incrementForAvoidance);
  }
  return false;
}

/*
 * Sets the variable 'incrementForAvoidance' "intelligent" positive/negative
 */
uint8_t chooseIncrementAvoidance()
{
	float reason;
	// Randomly choose CW or CCW avoiding direction
	if (vision_vector[midpoint-1] < vision_vector[midpoint+1]){
		incrementForAvoidance = -10.0;
		reason = 1;
	} else if (vision_vector[midpoint-1] > vision_vector[midpoint+1]){
		incrementForAvoidance = 10.0;
		reason = 2;
	} else {
		reason = 3;
		int r = rand() % 2;
		if (r == 0) {
			incrementForAvoidance = 10.0;
		} else {
			incrementForAvoidance = -10.0;
		}
	}
	VERBOSE_PRINT("Set avoidance increment to: %f, reason %f\n", incrementForAvoidance, reason);
	return false;
}

/*
 * Sets the variable 'obstaclesPresent' based on the vision vector and a threshold
 * to indicate where obstacles are located.
 */
void arcCheckObstacles(uint8_t arcThreshold)
{
	for (uint8_t i=0;i<5;i++)
	{
		if (vision_vector[i] >= arcThreshold) {
			obstaclesPresent[i] = 1;
		} else {
			obstaclesPresent[i] = 0;
		}
	}
}
