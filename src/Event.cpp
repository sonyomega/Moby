/***************************************************************************
 * Copyright 2011 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#include <cmath>
#include <algorithm>
#include <vector>
#include <queue>
#include <map>
#include <fstream>

#ifdef USE_OSG
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/Shape>
#include <osg/ShapeDrawable>
#include <osg/MatrixTransform>
#include <osg/Material>
#endif

#include <Moby/Constants.h>
#include <Moby/CompGeom.h>
#include <Moby/SingleBody.h>
#include <Moby/RigidBody.h>
#include <Moby/ArticulatedBody.h>
#include <Moby/CollisionGeometry.h>
#include <Moby/Log.h>
#include <Moby/AAngle.h>
#include <Moby/Event.h>

using namespace Moby;
using std::list;
using std::vector;
using std::map;
using std::multimap;
using std::set;
using std::endl;
using boost::shared_ptr;

/// Creates an empty event 
Event::Event()
{
  tol = NEAR_ZERO;              // default collision tolerance
  t_true = (Real) -1.0;
  event_type = eNone;
  limit_dof = std::numeric_limits<unsigned>::max();
  limit_epsilon = (Real) 0.0;
  limit_upper = false;
  limit_impulse = (Real) 0.0;
  contact_normal = ZEROS_3;
  contact_impulse = ZEROS_3;
  contact_point = ZEROS_3;
  contact_mu_coulomb = (Real) 0.0;
  contact_mu_viscous = (Real) 0.0;
  contact_epsilon = (Real) 0.0;
  contact_NK = 4;
}

Event& Event::operator=(const Event& e)
{
  tol = e.tol;
  t_true = e.t_true;
  t = e.t;
  event_type = e.event_type;
  limit_epsilon = e.limit_epsilon;
  limit_dof = e.limit_dof;
  limit_upper = e.limit_upper;
  limit_impulse = e.limit_impulse;
  limit_joint = e.limit_joint;
  contact_normal = e.contact_normal;
  contact_geom1 = e.contact_geom1;
  contact_geom2 = e.contact_geom2;
  contact_point = e.contact_point;
  contact_impulse = e.contact_impulse;
  contact_mu_coulomb = e.contact_mu_coulomb;
  contact_mu_viscous = e.contact_mu_viscous;
  contact_epsilon = e.contact_epsilon;
  contact_NK = e.contact_NK;
  contact_tan1 = e.contact_tan1;
  contact_tan2 = e.contact_tan2;
  constraint_nimpulse.copy_from(e.constraint_nimpulse);
  constraint_fimpulse.copy_from(e.constraint_fimpulse);
  constraint_joint = e.constraint_joint;

  return *this;
}

/// Sets the contact parameters for this event
void Event::set_contact_parameters(const ContactParameters& cparams)
{
  contact_mu_coulomb = cparams.mu_coulomb;
  contact_mu_viscous = cparams.mu_viscous;
  contact_epsilon = cparams.epsilon;
  contact_NK = cparams.NK;
}

/// Computes the velocity of this event
/**
 * Positive velocity indicates separation, negative velocity indicates
 * impact, zero velocity indicates rest.
 */
Real Event::calc_event_vel() const
{
  if (event_type == eContact)
  {
    assert(contact_geom1 && contact_geom2);
    SingleBodyPtr sb1 = contact_geom1->get_single_body();
    SingleBodyPtr sb2 = contact_geom2->get_single_body();
    assert(sb1 && sb2);
    return sb1->calc_point_vel(contact_point, contact_normal) - sb2->calc_point_vel(contact_point, contact_normal);
  }
  else if (event_type == eLimit)
  {
    Real qd = limit_joint->qd[limit_dof];
    return (limit_upper) ? -qd : qd;
  }
  else
    assert(false);
}  

/// Sends the event to the specified stream
std::ostream& Moby::operator<<(std::ostream& o, const Event& e)
{
  o << "TOI: " << e.t << std::endl;

  switch (e.event_type)
  {
    case Event::eNone:
      o << "(event type: none)" << std::endl;
      return o;

    case Event::eLimit:
      o << "(event type: joint limit)" << std::endl;
      break;

    case Event::eContact:
      o << "(event type: contact)" << std::endl;
      break;

    case Event::eConstraint:
      o << "(event type: constraint)" << std::endl;
  }

  if (e.event_type == Event::eContact)
  {
    if (e.contact_geom1)
    {
      SingleBodyPtr sb1(e.contact_geom1->get_single_body());
      if (sb1)
      {
        o << "body1: " << sb1->id << std::endl;
      }
      else
        o << "body1: (undefined)" << std::endl;
    }
    else
      o << "geom1: (undefined)" << std::endl;
  
    if (e.contact_geom2)
    {
      SingleBodyPtr sb2(e.contact_geom2->get_single_body());
      if (sb2)
      {
        o << "body2: " << sb2->id << std::endl;
      }    
      else
        o << "body2: (undefined)" << std::endl;
     }
    else
      o << "geom2: (undefined)" << std::endl;

    o << "contact point: " << e.contact_point << std::endl;
    o << "normal: " << e.contact_normal << std::endl;

    // determine the relative normal velocity at the contact point
    // get the rigid bodies of the contact
    if (e.contact_geom1 && e.contact_geom2)
    {
      SingleBodyPtr sb1(e.contact_geom1->get_single_body());
      SingleBodyPtr sb2(e.contact_geom2->get_single_body());
      if (sb1 && sb2)
      {
        Real cp1 = sb1->calc_point_vel(e.contact_point, e.contact_normal);
        Real cp2 = sb2->calc_point_vel(e.contact_point, e.contact_normal);
        Real rvel = cp1 - cp2; 
        o << "relative normal velocity: " << rvel << std::endl;
      }
    }
  }

  return o;
}

#ifdef USE_OSG
/// Copies this matrix to an OpenSceneGraph Matrixd object
static void to_osg_matrix(const Matrix4& src, osg::Matrixd& tgt)
{
  const unsigned X = 0, Y = 1, Z = 2, W = 3;
  for (unsigned i=X; i<= W; i++)
    for (unsigned j=X; j<= Z; j++)
      tgt(j,i) = src(i,j);

  // set constant values of the matrix
  tgt(X,W) = tgt(Y,W) = tgt(Z,W) = (Real) 0.0;
  tgt(W,W) = (Real) 1.0;
}

/// Makes a contact visualizable
osg::Node* Event::to_visualization_data() const
{
  const float CONE_HEIGHT = .2f;
  const float CONE_RADIUS = .2f;
  const unsigned X = 0, Y = 1, Z = 2;

  // setup the transformation matrix for the cone
  Vector3 x_axis, z_axis;
  Vector3::determine_orthonormal_basis(contact_normal, x_axis, z_axis);
  Matrix3 R;
  R.set_column(X, x_axis);
  R.set_column(Y, contact_normal);
  R.set_column(Z, -z_axis);
  Vector3 x = contact_point + contact_normal;
  Matrix4 T(&R, &x);

  // setup the transform node for the cone
  osg::Matrixd m;
  to_osg_matrix(T, m);
  osg::MatrixTransform* transform = new osg::MatrixTransform;
  transform->setMatrix(m);

  // create the new color
  osg::Material* mat = new osg::Material;
  const float RED = (float) rand() / RAND_MAX;
  const float GREEN = (float) rand() / RAND_MAX;
  const float BLUE = (float) rand() / RAND_MAX;
  mat->setColorMode(osg::Material::DIFFUSE);
  mat->setDiffuse(osg::Material::FRONT, osg::Vec4(RED, GREEN, BLUE, 1.0f));
  transform->getOrCreateStateSet()->setAttribute(mat);

  // create the line
  osg::Geometry* linegeom = new osg::Geometry;
  osg::Vec3Array* varray = new osg::Vec3Array;
  linegeom->setVertexArray(varray);  
  varray->push_back(osg::Vec3((float) contact_point[X], (float) contact_point[Y], (float) contact_point[Z]));
  varray->push_back(osg::Vec3((float) contact_point[X] + (float) contact_normal[X], (float) contact_point[Y] + (float) contact_normal[Y], (float) contact_point[Z] + (float) contact_normal[Z]));
  osg::Geode* geode = new osg::Geode;
  geode->addDrawable(linegeom);

  // create the cone
  osg::Cone* cone = new osg::Cone;
  cone->setRadius(CONE_RADIUS);
  cone->setHeight(CONE_HEIGHT);
  geode->addDrawable(new osg::ShapeDrawable(cone));

  // add the geode
  transform->addChild(geode);

  return transform;
}
#endif

/// Given a vector of events, determines all of the sets of connected events
/**
 * A set of connected events is the set of all events such that, for a
 * given event A in the set, there exists another event B for which A
 * and B share at least one rigid body.  
 * \param events the list of events
 * \param groups the islands of connected events on return
 */
void Event::determine_connected_events(const vector<Event>& events, list<list<Event*> >& groups)
{
  FILE_LOG(LOG_EVENT) << "Event::determine_connected_contacts() entered" << std::endl;

  // clear the groups
  groups.clear();

  // copy the list of events -- only ones with geometry
  list<Event*> events_copy;
  BOOST_FOREACH(const Event& e, events)
    if (e.event_type != Event::eNone)
      events_copy.push_back((Event*) &e);
  
  // The way that we'll determine the event islands is to treat each rigid
  // body present in the events as a node in a graph; nodes will be connected
  // to other nodes if (a) they are both present in event or (b) they are
  // part of the same articulated body.  Nodes will not be created for disabled
  // bodies.
  set<SingleBodyPtr> nodes;
  multimap<SingleBodyPtr, SingleBodyPtr> edges;
  typedef multimap<SingleBodyPtr, SingleBodyPtr>::const_iterator EdgeIter;

  // get all single bodies present in the events
  for (list<Event*>::const_iterator i = events_copy.begin(); i != events_copy.end(); i++)
  {
    if ((*i)->event_type == Event::eContact)
    {
      SingleBodyPtr sb1((*i)->contact_geom1->get_single_body());
      SingleBodyPtr sb2((*i)->contact_geom2->get_single_body());
      if (sb1->is_enabled())
        nodes.insert(sb1);
      if (sb2->is_enabled())
        nodes.insert(sb2);
      if (sb1->is_enabled() && sb2->is_enabled())
      {
        edges.insert(std::make_pair(sb1, sb2));
        edges.insert(std::make_pair(sb2, sb1));
      }
    }
    else if ((*i)->event_type == Event::eLimit)
    {
      RigidBodyPtr inboard = (*i)->limit_joint->get_inboard_link();
      RigidBodyPtr outboard = (*i)->limit_joint->get_outboard_link();
      nodes.insert(inboard);
      nodes.insert(outboard);
    }
    else if ((*i)->event_type == Event::eConstraint)
    {
      RigidBodyPtr inboard = (*i)->constraint_joint->get_inboard_link();
      RigidBodyPtr outboard = (*i)->constraint_joint->get_outboard_link();
      nodes.insert(inboard);
      nodes.insert(outboard);
    }
    else 
      assert(false);
  }

  FILE_LOG(LOG_EVENT) << " -- single bodies in events:" << std::endl;
  if (LOGGING(LOG_EVENT))
    for (set<SingleBodyPtr>::const_iterator i = nodes.begin(); i != nodes.end(); i++)
      FILE_LOG(LOG_EVENT) << "    " << (*i)->id << std::endl;
  FILE_LOG(LOG_EVENT) << std::endl;

  // add connections between articulated rigid bodies -- NOTE: don't process
  // articulated bodies twice!
  set<ArticulatedBodyPtr> ab_processed;
  BOOST_FOREACH(SingleBodyPtr sb, nodes)
  {
    // if the body is not part of an articulated body, skip it
    ArticulatedBodyPtr abody = sb->get_articulated_body();
    if (!abody)
      continue;

    // see whether it has already been processed
    if (ab_processed.find(abody) != ab_processed.end())
      continue;

    // indicate that the articulated body will now have been processed
    ab_processed.insert(abody);

    // get all links in the articulated body
    const vector<RigidBodyPtr>& links = abody->get_links();

    // add edges between all pairs for which there are links
    vector<RigidBodyPtr>::const_iterator j, k;
    for (j = links.begin(); j != links.end(); j++)
    {
      // no sense iterating over all other links if link pointed to by j is
      // not a node
      if (nodes.find(*j) == nodes.end())
        continue;

      // iterate over all other nodes
      k = j;
      for (k++; k != links.end(); k++)
        if (nodes.find(*k) != nodes.end())
        {
          edges.insert(std::make_pair(*j, *k));
          edges.insert(std::make_pair(*k, *j));
        }
    }      
  }

  // Now, we'll remove nodes from the set until there are no more nodes.
  // For each removed node, we'll get add all events that contain the single 
  // body to the group; all neighboring nodes will then be processed.
  while (!nodes.empty())
  {
    // get the node from the front
    SingleBodyPtr node = *nodes.begin();

    // add a list to the contact groups
    groups.push_back(list<Event*>());
    FILE_LOG(LOG_EVENT) << " -- events in group: " << std::endl;

    // create a node queue, with this node added
    std::queue<SingleBodyPtr> node_q;
    node_q.push(node);

    // loop until the queue is empty
    while (!node_q.empty())
    {
      // get the node off of the front of the node queue
      node = node_q.front();
      node_q.pop();

      // erase the node from the set of nodes
      nodes.erase(node);

      // add all neighbors of the node that have not been processed already 
      // to the node queue
      std::pair<EdgeIter, EdgeIter> neighbors = edges.equal_range(node);
      for (EdgeIter i = neighbors.first; i != neighbors.second; i++)
        if (nodes.find(i->second) != nodes.end())
          node_q.push(i->second);

      // loop through all remaining events
      for (list<Event*>::iterator i = events_copy.begin(); i != events_copy.end(); )
      {
        if ((*i)->event_type == Event::eContact)
        {
          SingleBodyPtr sb1((*i)->contact_geom1->get_single_body());
          SingleBodyPtr sb2((*i)->contact_geom2->get_single_body());

          // see whether one of the bodies is equal to the node
          if (sb1 == node || sb2 == node)
          {
            groups.back().push_back(*i);
            i = events_copy.erase(i);
            continue;
          }
          else
            i++;
        }
        else if ((*i)->event_type == Event::eLimit)
        {
          RigidBodyPtr inboard = (*i)->limit_joint->get_inboard_link();
          RigidBodyPtr outboard = (*i)->limit_joint->get_outboard_link();
          if (inboard == node || outboard == node)
          {
            groups.back().push_back(*i);
            i = events_copy.erase(i);
            continue;
          }
          else
            i++;
        }
        else if ((*i)->event_type == Event::eConstraint)
        {
          RigidBodyPtr inboard = (*i)->constraint_joint->get_inboard_link();
          RigidBodyPtr outboard = (*i)->constraint_joint->get_outboard_link();
          if (inboard == node || outboard == node)
          {
            groups.back().push_back(*i);
            i = events_copy.erase(i);
            continue;
          }
          else
            i++;
        }
        else
          assert(false);
      }
    }
  }

  FILE_LOG(LOG_EVENT) << "Event::determine_connected_events() exited" << std::endl;
}

/*
/// Modified Gaussian elimination with partial pivoting -- computes half-rank at the same time - this version designed to work with "full" 3*NC x 3*NC matrices
unsigned Event::gauss_elim(MatrixN& A, vector<unsigned>& piv)
{
  const unsigned NROWS = A.rows(), NCOLS = A.columns();
  const unsigned NPOSVARS = (NCOLS-1)/3;

  FILE_LOG(LOG_EVENT) << "matrix before modified Gaussian elimination:" << std::endl << A;

  // equilibrate the rows of A 
  for (unsigned j=0; j< NROWS; j++)
  {
    Real* col = &A(j,0);
    Real max_val = (Real) 0.0;
    for (unsigned i=0, k=0; i< NCOLS; i++, k+= NROWS)
      max_val = std::max(max_val, std::fabs(col[k]));
    assert(max_val > (Real) 0.0);
    CBLAS::scal(NCOLS, (Real) 1.0/max_val, col, NROWS);
  }

  // setup epsilon
  const Real EPS = NCOLS * NEAR_ZERO;

  // resize the pivots array
  piv.resize(NROWS);
  for (unsigned i=0; i< NROWS; i++)
    piv[i] = i;

  // setup swpi
  unsigned swpi = 0;

  // iterate over the positive variables
  for (unsigned i=0; i< NPOSVARS; i++)
  {
    // get the pointer to the column 
    BlockIterator col = A.block_start(0, NROWS, i,i+1);

    // find the largest positive element in the column
    Real largest = col[piv[swpi]];
    unsigned largest_index = swpi;
    for (unsigned k=swpi+1; k< NROWS; k++)
      if (col[piv[k]] > largest)
      {
        largest_index = k;
        largest = col[piv[k]];
      }

    // continue processing only if largest positive element is greater than 0 
    if (largest > EPS)
    {
      // swap the pivots
      std::swap(piv[swpi], piv[largest_index]);

      // reduce the rows 
      const Real* elm = &A(piv[swpi], i);
      for (unsigned k=swpi+1; k< NROWS; k++)
      {
        Real* elm_k = &A(piv[k], i);
        if (*elm_k > EPS)
        {
          Real scal = -*elm_k / *elm;
          CBLAS::axpy(NCOLS-i, scal, elm, NROWS, &A(piv[k],i), NROWS);
        }
      }

      // update the swap pivot
      swpi++;

      // quit if swpi too large
      if (swpi == NROWS)
        return swpi;
    }

    // find the largest negative element in the column
    largest = col[piv[swpi]];
    largest_index = swpi;
    for (unsigned k=swpi+1; k< NROWS; k++)
      if (col[piv[k]] < largest)
      {
        largest_index = k;
        largest = col[piv[k]];
      }

    // only continue processing if largest negative element is less than 0
    if (largest < -EPS)
    {
      // swap the pivots
      std::swap(piv[swpi], piv[largest_index]);
      
      // reduce the rows
      const Real* elm = &A(piv[swpi], i);
      for (unsigned k=swpi+1; k< NROWS; k++)
      {
        Real* elm_k = &A(piv[k], i);
        if (*elm_k < -EPS)
        {
          Real scal = -*elm_k / *elm;
          CBLAS::axpy(NCOLS-i, scal, elm, NROWS, &A(piv[k], i), NROWS);
        }
      }

      // update the swap pivot
      swpi++;

      // quit if swpi too large
      if (swpi == NROWS)
        return swpi;
    }
  }

  // iterate over the real variables
  for (unsigned i=NPOSVARS; i< NCOLS; i++)
  {
    // get the pointer to the column 
    BlockIterator col = A.block_start(0, NROWS, i,i+1);

    // find the largest element in the column
    Real largest = col[piv[swpi]];
    unsigned largest_index = swpi;
    for (unsigned k=swpi+1; k< NROWS; k++)
      if (std::fabs(col[piv[k]]) > largest)
      {
        largest_index = k;
        largest = std::fabs(col[piv[k]]);
      }

    // continue processing only if largest positive element is greater than 0 
    if (largest > EPS)
    {
      // swap the pivots
      std::swap(piv[swpi], piv[largest_index]);

      // reduce the rows 
      const Real* elm = &A(piv[swpi], i);
      for (unsigned k=swpi+1; k< NROWS; k++)
      {
        Real* elm_k = &A(piv[k], i);
        Real scal = -*elm_k / *elm;
        CBLAS::axpy(NCOLS-i, scal, elm, NROWS, &A(piv[k],i), NROWS);
      }

      // update the swap pivot
      swpi++;

      // quit if swpi too large
      if (swpi == NROWS)
        break;
    }
  }

  FILE_LOG(LOG_EVENT) << "matrix after modified Gaussian elimination:" << std::endl << A;
  FILE_LOG(LOG_EVENT) << "rank: " << swpi << std::endl;
  if (LOGGING(LOG_EVENT))
  {
    std::ostringstream oss;
    oss << "pivots:";
    for (unsigned i=0; i< piv.size(); i++)
      oss << " " << piv[i];
    FILE_LOG(LOG_EVENT) << oss.str() << std::endl;
  }

  return swpi;
}
*/

/// Determines whether the new contact event is redundant 
void Event::redundant_contacts(const MatrixN& Jc, const MatrixN& Dc, vector<unsigned>& nr_indices)
{
  SAFESTATIC MatrixN workM;
  SAFESTATIC VectorN x;
  SAFESTATIC LPParams lp;
  SAFESTATIC vector<unsigned> row_indices, Dc_row_indices;

  // get # of contacts
  const unsigned NC = Jc.rows();

  // setup row indices
  for (unsigned i=0; i< nr_indices.size(); )
  {
    // copy nr_indices
    row_indices = nr_indices;

    // remove the i'th contact
    row_indices.erase(row_indices.begin()+i);

    // select appropriate rows of Jc
    Jc.select_rows(row_indices.begin(), row_indices.end(), workM);

    // see wehether there are any redundant contacts
    MatrixN::transpose(workM, lp.A);
    lp.n = row_indices.size();

    // setup lower and upper bounds on variables
    lp.l.set_zero(lp.n);
    lp.u.set_zero(0);

    // setup 'c' variable (l1-norm)
    lp.c.set_one(lp.n);

    // setup LP M and q variables
    lp.M.resize(0,lp.n);
    lp.q.resize(0);

    // resize x
    x.resize(lp.n);

    // must be able to solve one LPs (one for normal)
    Jc.get_row(nr_indices[i], lp.b);
  
    // solve the LP (if not possible to solve, contact is not redundant)
    if (!Optimization::lp_simplex(lp, x))
    {
      i++;
      continue;
    }
/*
    // get rank proposed row rank of Dc
    Dc_row_indices.clear();
    for (unsigned j=0; j< row_indices.size(); j++)
    {
      Dc_row_indices.push_back(row_indices[j]*2);
      Dc_row_indices.push_back(row_indices[j]*2+1);
    } 
    Dc.select_rows(Dc_row_indices.begin(), Dc_row_indices.end(), workM);
    unsigned rank = LinAlg::calc_rank(workM);

    // now, compute rank plus indices we took out
    Dc_row_indices.push_back(nr_indices[i]*2);
    Dc_row_indices.push_back(nr_indices[i]*2+1);
    insertion_sort(Dc_row_indices.begin(), Dc_row_indices.end());
    Dc.select_rows(Dc_row_indices.begin(), Dc_row_indices.end(), workM);
    unsigned new_rank = LinAlg::calc_rank(workM);

    if (new_rank > rank)
    {
      i++;
      continue;
    }
    else 
*/
      nr_indices = row_indices;
  }
}

/// Computes normal and contact Jacobians for a body
void Event::compute_contact_jacobian(const Event& e, MatrixN& Jc, MatrixN& iM_JcT, unsigned ci, const map<DynamicBodyPtr, unsigned>& gc_indices)
{
  map<DynamicBodyPtr, unsigned>::const_iterator miter;
  SAFESTATIC FastThreadable<VectorN> tmpv, tmpv2;

  // get the two bodies
  SingleBodyPtr sb1 = e.contact_geom1->get_single_body();
  SingleBodyPtr sb2 = e.contact_geom2->get_single_body();

  // get the super bodies
  DynamicBodyPtr ab1 = sb1->get_articulated_body();
  DynamicBodyPtr ab2 = sb2->get_articulated_body();
  DynamicBodyPtr super1 = (ab1) ? ab1 : sb1;
  DynamicBodyPtr super2 = (ab2) ? ab2 : sb2;

  // process the first body
  miter = gc_indices.find(super1);
  if (miter != gc_indices.end())
  {
    const unsigned index = miter->second;

    // compute the 'r' vector
    Vector3 r = e.contact_point - sb1->get_position();

    // convert the normal force to generalized forces
    super1->convert_to_generalized_force(DynamicBody::eAxisAngle, sb1, e.contact_normal, Vector3::cross(r, e.contact_normal), tmpv());
    Jc.set_sub_mat(ci, index, tmpv(), true);

    // compute iM_JcT components
    super1->solve_generalized_inertia(DynamicBody::eAxisAngle, tmpv(), tmpv2());
    iM_JcT.set_sub_mat(index, ci, tmpv2());
  }

  // process the second body
  miter = gc_indices.find(super2);
  if (miter != gc_indices.end())
  {
    const unsigned index = miter->second;

    // compute the 'r' vector
    Vector3 r = e.contact_point - sb2->get_position();

    // convert the normal force to generalized forces
    super2->convert_to_generalized_force(DynamicBody::eAxisAngle, sb2, -e.contact_normal, Vector3::cross(r, -e.contact_normal), tmpv());
    Jc.set_sub_mat(ci, index, tmpv(), true);

    // compute iM_JcT components
    super2->solve_generalized_inertia(DynamicBody::eAxisAngle, tmpv(), tmpv2());
    iM_JcT.set_sub_mat(index, ci, tmpv2());
  }
}

/// Computes normal and contact Jacobians for a body
void Event::compute_contact_jacobians(const Event& e, VectorN& Nc, VectorN& Dcs, VectorN& Dct)
{
  SAFESTATIC FastThreadable<VectorN> Nc1, Nc2, Dcs1, Dcs2, Dct1, Dct2;

  // get the two bodies
  SingleBodyPtr sb1 = e.contact_geom1->get_single_body();
  SingleBodyPtr sb2 = e.contact_geom2->get_single_body();

  // make sure that the two bodies are ordered
  if (sb2 < sb1)
    std::swap(sb1, sb2);

  // get the super bodies
  DynamicBodyPtr ab1 = sb1->get_articulated_body();
  DynamicBodyPtr ab2 = sb2->get_articulated_body();
  DynamicBodyPtr super1 = (ab1) ? ab1 : sb1;
  DynamicBodyPtr super2 = (ab2) ? ab2 : sb2;

  // get the total number of GC's
  const unsigned GC1 = super1->num_generalized_coordinates(DynamicBody::eAxisAngle);
  const unsigned GC2 = super2->num_generalized_coordinates(DynamicBody::eAxisAngle);
  const unsigned NGC = (super1 != super2) ? GC1 + GC2 : GC1;

  // zero the Jacobian vectors
  Nc.set_zero(NGC);
  Dcs.set_zero(NGC);
  Dct.set_zero(NGC);

  // process the first body
  // compute the 'r' vector
  Vector3 r1 = e.contact_point - sb1->get_position();

  // convert the normal force to generalized forces
  super1->convert_to_generalized_force(DynamicBody::eAxisAngle, sb1, e.contact_normal, Vector3::cross(r1, e.contact_normal), Nc1());

  // convert first tangent direction to generalized forces
  super1->convert_to_generalized_force(DynamicBody::eAxisAngle, sb1, e.contact_tan1, Vector3::cross(r1, e.contact_tan1), Dcs1());

  // convert second tangent direction to generalized forces
  super1->convert_to_generalized_force(DynamicBody::eAxisAngle, sb1, e.contact_tan2, Vector3::cross(r1, e.contact_tan2), Dct1());

  // process the second body
  // compute the 'r' vector
  Vector3 r2 = e.contact_point - sb2->get_position();

  // convert the normal force to generalized forces
  super2->convert_to_generalized_force(DynamicBody::eAxisAngle, sb2, -e.contact_normal, Vector3::cross(r2, -e.contact_normal), Nc2());

  // convert first tangent direction to generalized forces
  super2->convert_to_generalized_force(DynamicBody::eAxisAngle, sb2, -e.contact_tan1, Vector3::cross(r2, -e.contact_tan1), Dcs2());

  // convert second tangent direction to generalized forces
  super2->convert_to_generalized_force(DynamicBody::eAxisAngle, sb2, -e.contact_tan2, Vector3::cross(r2, -e.contact_tan2), Dct2());

  // now, set the proper elements in the Jacobian
  if (super1 == super2)
  {
    Nc1() += Nc2();
    Dcs1() += Dcs2();
    Dct1() += Dct2();
    Nc.copy_from(Nc1());
    Dcs.copy_from(Dcs1());
    Dct.copy_from(Dct1());
  }
  else
  {
    Nc.set_sub_vec(0, Nc1());
    Dcs.set_sub_vec(0, Dcs1());
    Dct.set_sub_vec(0, Dct1());
    Nc.set_sub_vec(GC1, Nc2());
    Dcs.set_sub_vec(GC1, Dcs2());
    Dct.set_sub_vec(GC1, Dct2());
  }
}

/// Computes a minimal set of contact events
void Event::determine_minimal_set(list<Event*>& group)
{
  FILE_LOG(LOG_EVENT) << "Event::determine_minimal_set() entered" << std::endl;
  FILE_LOG(LOG_EVENT) << " -- initial number of events: " << group.size() << std::endl;

  // get the number of contact events and total number of events
  list<Event*>::iterator start = group.begin();
  unsigned NC = 0, NE = 0;
  while (start != group.end() && ++NE)
  {
    if ((*start)->event_type == Event::eContact)
      NC++;
    start++;
  }

  // if there is one or fewer contacts, or very few events, quit now
  if (NC <= 1 || NE < 4)
  {
    FILE_LOG(LOG_EVENT) << " -- initial/final number of contacts: " << NC << std::endl;
    FILE_LOG(LOG_EVENT) << " -- initial/final number of events: " << NE << std::endl;
    return;
  }

  // determine the number of gc's in the group
  unsigned NGC = 0;
  map<DynamicBodyPtr, unsigned> gc_index;
  vector<DynamicBodyPtr> supers;
  for (list<Event*>::const_iterator i = group.begin(); i != group.end(); i++)
  {
    supers.clear();
    (*i)->get_super_bodies(std::back_inserter(supers));
    for (unsigned j=0; j< supers.size(); j++)
      if (gc_index.find(supers[j]) == gc_index.end())
      {
        gc_index[supers[j]] = NGC;
        NGC += supers[j]->num_generalized_coordinates(DynamicBody::eAxisAngle);
      }
  }

  // setup contact Jacobian and contact space inertia matrix
  MatrixN Jc(NC, NGC), Jc_iM_JcT(NC, NC), iM_JcT(NGC, NC), workM;
  VectorN workv, workv2;

  // clear Jacobian
  Jc.set_zero();

  // setup the contact index
  unsigned ci = 0;

  // loop through the remainder of contacts
  for (list<Event*>::iterator i = group.begin(); i != group.end(); i++)
  {
    // if this isn't a contact event, skip it
    if ((*i)->event_type != Event::eContact)
      continue;

    // get the Jacobian for this contact
    compute_contact_jacobian(**i, Jc, iM_JcT, ci++, gc_index);
  }

  // compute contact space inertia matrix
  Jc.mult(iM_JcT, Jc_iM_JcT);

  // setup selection indices for contact 0
  vector<unsigned> sel;
  sel.push_back(0);

  // loop over all contacts
  for (unsigned i=1; i< NC; i++)
  {
    FILE_LOG(LOG_EVENT) << " examining contact point " << i << endl;

    // get the appropriate row of Jc_iM_JcT
    Jc_iM_JcT.get_row(i, workv);
    workv.select(sel.begin(), sel.end(), workv2);

    // verify that there is a positive component
    if (*std::min_element(workv2.begin(), workv2.end()) <= (Real) 0.0)
      sel.push_back(i);    
  } 

  // loop through contacts again
  ci = 0;
  for (list<Event*>::iterator i = group.begin(); i != group.end(); )
  {
    // see whether this index exists in the pivots that are left over
    if (std::binary_search(sel.begin(), sel.end(), ci++))
      i++; 
    else
      i = group.erase(i);
  }

  FILE_LOG(LOG_EVENT) << " -- final number of events: " << group.size() << std::endl;
} 

/**
 * Complexity of computing a minimal set:
 * N = # of contacts, NGC = # of generalized coordinates
 * NGC << N
 *
 * Cost of computing J*inv(M)*J', J*v for one contacts: NGC^3
 *                                    for R contacts: NGC^3 + 2*NGC^2*R
 * Cost of Modified Gauss elimination for M contacts (M < NGC), 
        M x NGC matrix: M^2*NGC
 *
 * Overall cost: 2*NGC^2*R (for R > NGC, where many redundant contact points
                            present) + NGC^3
 * therefore generalized coordinates are the limiting factor...
 */
/*
/// Computes a minimal set of contact events
void Event::determine_minimal_set(list<Event*>& group)
{
  // if there are very few events, quit now
  if (group.size() <= 4)
    return;

  FILE_LOG(LOG_EVENT) << "Event::determine_minimal_set() entered" << std::endl;
  FILE_LOG(LOG_EVENT) << " -- initial number of events: " << group.size() << std::endl;

  // setup a mapping from pairs of single bodies to groups of events
  map<sorted_pair<SingleBodyPtr>, list<Event*> > contact_groups;

  // move all contact events into separate groups
  for (list<Event*>::iterator i = group.begin(); i != group.end(); )
  {
    if ((*i)->event_type == Event::eContact)
    {
      // get the two bodies
      SingleBodyPtr sb1 = (*i)->contact_geom1->get_single_body();
      SingleBodyPtr sb2 = (*i)->contact_geom2->get_single_body();

      // move the contact to the group
      contact_groups[make_sorted_pair(sb1, sb2)].push_back(*i);
      i = group.erase(i);
    }
    else
      i++;
  }

  // process each group independently, then recombine
  for (map<sorted_pair<SingleBodyPtr>, list<Event*> >::iterator i = contact_groups.begin(); i != contact_groups.end(); i++)
  {
    determine_minimal_subset(i->second);
    group.insert(group.end(), i->second.begin(), i->second.end()); 
  }
}

/// Computes a minimal subset of contact events
void Event::determine_minimal_subset(list<Event*>& group)
{
  FILE_LOG(LOG_EVENT) << "Event::determine_minimal_set() entered" << std::endl;
  FILE_LOG(LOG_EVENT) << " -- initial number of events: " << group.size() << std::endl;

  // if there is one or fewer contacts quit now
  if (group.empty() || group.front() == group.back())
  {
    FILE_LOG(LOG_EVENT) << " -- initial/final number of contacts: " << group.size() << std::endl;
    return;
  }

  // get the number of contact events
  const unsigned NC = group.size();
  list<Event*>::iterator start = group.begin();

  // get the two bodies
  SingleBodyPtr sb1 = (*group.begin())->contact_geom1->get_single_body();
  SingleBodyPtr sb2 = (*group.begin())->contact_geom2->get_single_body();

  // get the super bodies
  DynamicBodyPtr ab1 = sb1->get_articulated_body();
  DynamicBodyPtr ab2 = sb2->get_articulated_body();
  DynamicBodyPtr super1 = (ab1) ? ab1 : sb1;
  DynamicBodyPtr super2 = (ab2) ? ab2 : sb2;

  // get the total number of GC's
  const unsigned GC1 = super1->num_generalized_coordinates(DynamicBody::eAxisAngle);
  const unsigned GC2 = super2->num_generalized_coordinates(DynamicBody::eAxisAngle);
  const unsigned NGC = (super1 != super2) ? GC1 + GC2 : GC1;

  // setup contact Jacobians
  MatrixN Jc(NC, NGC), Dc(NC*2, NGC), workM;
  VectorN workv, Jc_vec, Dc1_vec, Dc2_vec;

  // clear both Jacobians
  Jc.set_zero();
  Dc.set_zero();

  // setup the contact index
  unsigned ci = 0;

  // loop through the remainder of contacts
  for (list<Event*>::iterator i = group.begin(); i != group.end(); i++, ci++)
  {
    // get the Jacobians (normal & tangential) for this contact
    compute_contact_jacobians(**i, Jc_vec, Dc1_vec, Dc2_vec);

    // set the rows of the Jacobians
    Jc.set_row(ci, Jc_vec);
    Dc.set_row(ci*2, Dc1_vec);
    Dc.set_row(ci*2+1, Dc2_vec);
  }

  FILE_LOG(LOG_EVENT) << "contact Jacobian: " << std::endl << Jc;

  // setup selection indices for contact 0
  vector<unsigned> sel;
  sel.push_back(0);

  // loop over all contacts
  for (unsigned i=1; i< NC; i++)
  {
    FILE_LOG(LOG_EVENT) << " examining contact point " << i << endl;
    sel.push_back(i);

    // see whether the normal component of the contact is redundant 
    redundant_contacts(Jc, Dc, sel);
  } 

  // loop through contacts again
  ci = 0;
  for (list<Event*>::iterator i = group.begin(); i != group.end(); )
  {
    // see whether this index exists in the pivots that are left over
    if (std::binary_search(sel.begin(), sel.end(), ci++))
      i++; 
    else
      i = group.erase(i);
  }

  FILE_LOG(LOG_EVENT) << " -- final number of events: " << group.size() << std::endl;
}
*/

/// Removes groups of contacts that contain no impacts
void Event::remove_nonimpacting_groups(list<list<Event*> >& groups)
{
  typedef list<list<Event*> >::iterator ListIter;

  for (ListIter i = groups.begin(); i != groups.end(); )
  {
    // look for impact in list i
    bool impact_detected = false;
    BOOST_FOREACH(Event* e, *i)
    {
      if (e->is_impacting())
      {
        impact_detected = true;
        break;
      }
    }

    // if no impact in the list, remove the list
    if (!impact_detected)
    {
      ListIter j = i;
      j++;
      groups.erase(i);
      i = j;
    }
    else
      i++;
  }
}

/// Writes an event to the specified filename in VRML format for visualization
/**
 * \todo add a cone onto the arrows
 */
void Event::write_vrml(const std::string& fname, Real sphere_radius, Real normal_length) const
{
  const unsigned X = 0, Y = 1, Z = 2;
  std::ofstream out;
  
  // open the file for writing
  out.open(fname.c_str());
  if (out.fail())
    throw std::runtime_error("Unable to open file for writing in Event::write_vrml()");

  // write the VRML header
  out << "#VRML V2.0 utf8" << std::endl << std::endl;

  // *************************************************
  // first, write the contact point 
  // *************************************************

  // determine a random color that will be used for contact and normal
  Real c_x = (Real) rand() / RAND_MAX;
  Real c_y = (Real) rand() / RAND_MAX;
  Real c_z = (Real) rand() / RAND_MAX;

  // write the transform for the contact point
  out << "Transform {" << std::endl;
  out << "  translation "; 
  out << contact_point[X] << " " << contact_point[Y] << " " << contact_point[Z] << std::endl;
  out << "  children " << endl;

  // write the shape node, using default appearance
  out << "  Shape {" << std::endl;
  out << "    appearance Appearance { material Material {" << std::endl;
  out << "      transparency 0" << std::endl;
  out << "      shininess 0.2" << std::endl;
  out << "      ambientIntensity 0.2" << std::endl;
  out << "      emissiveColor 0 0 0" << std::endl;
  out << "      specularColor 0 0 0" << std::endl;
  out << "      diffuseColor " << c_x << " " << c_y << " " << c_z << std::endl;
  out << "      }}" << std::endl;

  // write the geometry (a sphere)
  out << "  geometry Sphere {" << std::endl; 
  out << "    radius " << sphere_radius << " }}} # end sphere, shape, transform " << std::endl;

  // *************************************************
  // now, write the normal
  // *************************************************

  // determine the normal edge
  Vector3 normal_start = contact_point;
  Vector3 normal_stop = normal_start + contact_normal*normal_length;

  // write the shape node, using default appearance
  out << "Shape {" << std::endl;
  out << "  appearance Appearance { material Material {" << std::endl;
  out << "    transparency 0" << std::endl;
  out << "    shininess 0.2" << std::endl;
  out << "    ambientIntensity 0.2" << std::endl;
  out << "    emissiveColor 0 0 0" << std::endl;
  out << "    specularColor 0 0 0" << std::endl;
  out << "    diffuseColor " << c_x << " " << c_y << " " << c_z << std::endl;
  out << "    }}" << std::endl;

  // write the geometry
  out << "  geometry IndexedLineSet {" << std::endl; 
  out << "    coord Coordinate { point [ ";
  out << normal_start[X] << " " << normal_start[Y] << " " << normal_start[Z] << ", ";
  out << normal_stop[X] << " " << normal_stop[Y] << " " << normal_stop[Z] << " ] } " << std::endl;
  out << "    coordIndex [ 0, 1, -1 ] }}" << std::endl;

  // **********************************************
  // determine the axis-angle rotation for the cone
  // **********************************************

  // first compose an arbitrary vector d
  Vector3 d(1,1,1);
  if (std::fabs(contact_normal[X]) > std::fabs(contact_normal[Y]))
  {
    if (std::fabs(contact_normal[X]) > std::fabs(contact_normal[Z]))
      d[X] = 0;
    else
      d[Z] = 0;
  }
  else
  {
    if (std::fabs(contact_normal[Y]) > std::fabs(contact_normal[Z]))
      d[Y] = 0;
    else
      d[Z] = 0;
  }
    
  // compute the cross product of the normal and the vector
  Vector3 x = Vector3::normalize(Vector3::cross(contact_normal, d));
  Vector3 y;
  y.copy_from(contact_normal);
  Vector3 z = Vector3::normalize(Vector3::cross(x, contact_normal));

  // compute theta and the axis of rotation
  Real theta = std::acos((x[X] + y[Y] + z[Z] - 1)/2);
  Vector3 axis(z[Y] - y[Z], x[Z] - z[X], y[X] - x[Y]);
  axis *= -(1.0/(2 * std::sin(theta)));
    
  // finally, write the cone to show the normal's direction
  out << "Transform {" << std::endl;
  out << "  rotation ";
   out  << axis[X] <<" "<< axis[1] <<" "<< axis[Z] <<" "<< theta << std::endl;
  out << "  translation ";
   out << normal_stop[X] <<" "<< normal_stop[Y] <<" "<< normal_stop[Z];
  out << std::endl;
  out << "  children [" << std::endl;
  out << "    Shape {" << std::endl;
  out << "      appearance Appearance { material Material {" << std::endl;
  out << "        transparency 0" << std::endl;
  out << "        shininess 0.2" << std::endl;
  out << "        ambientIntensity 0.2" << std::endl;
  out << "        emissiveColor 0 0 0" << std::endl;
  out << "        specularColor 0 0 0" << std::endl;
  out << "        diffuseColor " << c_x << " " << c_y << " " << c_z << std::endl;
  out << "        }}" << std::endl;
  out << "      geometry Cone {" << std::endl;
  out << "        bottomRadius " << sphere_radius << std::endl;
  out << "        height " << (normal_length * .1) << std::endl;
  out << "      } } ] }" << std::endl;
  out.close();
}

/// Determines the set of contact tangents
void Event::determine_contact_tangents()
{
  assert(event_type == Event::eContact);

  // determine an orthonormal basis using the two contact tangents
  Vector3::determine_orthonormal_basis(contact_normal, contact_tan1, contact_tan2);

/*
  // determine the projection matrix
  Matrix3 RT = Matrix3::transpose(CompGeom::calc_3D_to_2D_matrix(contact_normal));

  // setup some necessary constants
  const Real TWO_INV_NK = (Real) 1.0/contact_NK;
  const Real TWO_PI_INV_NK = M_PI * TWO_INV_NK;

  // setup the tangents
  for (unsigned i=0; i< contact_NK; i++)
  {
    Vector2 dx(std::cos(i*TWO_PI_INV_NK), std::sin(i*TWO_PI_INV_NK));
    contact_tangents[i] = CompGeom::to_3D(dx, RT);
  }
*/
}

/// Determines the type of event (impacting, resting, or separating)
Event::EventClass Event::determine_event_class() const
{
  // get the event velocity
  Real vel = calc_event_vel();

  FILE_LOG(LOG_SIMULATOR) << "-- event type: " << event_type << " velocity: " << vel << std::endl;

  // if the velocity is less than zero, we have
  // an impacting contact
  if (vel > tol)
    return eSeparating;
  else if (vel < -tol)
    return eImpacting;
  else
    return eResting;
}

/// Computes the event tolerance
/**
 * Positive velocity indicates separation, negative velocity indicates
 * impact, zero velocity indicates rest.
 */
Real Event::calc_event_tol() const
{
  if (event_type == eContact)
  {
    assert(contact_geom1 && contact_geom2);
    SingleBodyPtr sb1 = contact_geom1->get_single_body();
    SingleBodyPtr sb2 = contact_geom2->get_single_body();
    assert(sb1 && sb2);

    // get the moment arms
    Vector3 r1 = contact_point - sb1->get_position();
    Vector3 r2 = contact_point - sb2->get_position();

    // compute the velocity contributions
    Vector3 v1 = sb1->get_lvel() + Vector3::cross(sb1->get_avel(), r1);
    Vector3 v2 = sb2->get_lvel() + Vector3::cross(sb2->get_avel(), r2);

    return std::max((v1 - v2).norm(), (Real) 1.0);
  }
  else if (event_type == eLimit)
  {
    Real qd = limit_joint->qd[limit_dof];
    return std::max((Real) 1.0, std::fabs(qd));
  }
  else
    assert(false);
}

