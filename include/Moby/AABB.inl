/****************************************************************************
 * Copyright 2010 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

// For outputting description of AABB (primarily for debugging purposes)
inline std::ostream& operator<<(std::ostream& out, const AABB& a)
{
  out << " (address): " << &a << std::endl;
  out << " lower corner: " << a.minp << std::endl;
  out << " upper corner: " << a.maxp << std::endl;
  out << " volume: " << a.calc_volume() << std::endl;
  out << " children:";
  BOOST_FOREACH(BVPtr child, a.children)
    out << " " << child;
  out << std::endl;

  return out;
} 

/// Constructs an axis-aligned bounding box using a set of points
/**
 * \param begin an iterator to the beginning of a container of type Vector3
 * \param end an iterator to the end of a container of type Vector3
 */
template <class InputIterator>
AABB::AABB(InputIterator begin, InputIterator end)
{
  const unsigned THREE_D = 3;

  // init the corners of the box
  minp = *begin;
  begin++; 
  maxp = minp;

  while (begin != end)
  {
    for (unsigned i=0; i< THREE_D; i++)
    {
      if ((*begin)[i] < minp[i])
        minp[i] = (*begin)[i]; 
      if ((*begin)[i] > maxp[i])
        maxp[i] = (*begin)[i];
    }
    begin++;
  }
}

