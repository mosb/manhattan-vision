/*
 * math_utils.h
 *
 *  Created on: 2 Jun 2010
 *      Author: alexf
 */

#pragma once

#include "common_types.h"

namespace indoor_context {

// Compute log( exp(y1)+exp(y2)+...+exp(yn) )
double LogSumExp(const VecD& ys);

// Compute log( exp(y1)+exp(y2)+...+exp(yn) )
double LogSumExp(const double* ys, int n);

// Given a vector (y1, y2, ...), normalize it so that it represents
// the log of a valid distribution: i.e. the postcondition is:
//   exp(y1)+exp(y2)+...+exp(yn) = 1.
// Avoid underflow by using LogSumExp
void NormalizeLogDistr(VecD& ys);

// Tranform a vector of log likelihoods into a normalized
// distribution. This consists of exponentiating the terms and
// ensuring that they sum to 1, but this function uses LogSumExp to
// avoid underflow.
VecD LogLikelihoodToDistr(const VecD& ys);

// Convert an integer to a string of length W, padding on the left
// with '0' chars.
string PaddedInt(int x, int width);

}  // namespace indoor_context