/****************************************************************************
 * Copyright 2008 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _SINGULAR_EXCEPTION_H_
#define _SINGULAR_EXCEPTION_H_

#include <stdexcept>

namespace Moby {

/// Exception thrown when trying to invert/solve with a non-invertible matrix 
class SingularException : public std::runtime_error
{
  public:
    SingularException() : std::runtime_error("Matrix is singular") {}
}; // end class


} // end namespace

#endif

