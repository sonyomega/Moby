/****************************************************************************
 * Copyright 2010 Evan Drumwright
 * This library is distributed under the terms of the GNU Lesser General Public 
 * License (found in COPYING).
 ****************************************************************************/

#ifndef _PS_DEFORMABLE_BODY_H
#define _PS_DEFORMABLE_BODY_H

#include <Moby/DeformableBody.h>

namespace Moby {

/// A spring in the mass-spring system
struct Spring
{
  Spring()
  {
    kp = kv = rest_len = 0;
    node1 = node2 = std::numeric_limits<unsigned>::max();
  }

  /// The spring stiffness
  Real kp;

  /// The spring dampening
  Real kv;

  /// The first node connected by the spring
  unsigned node1;

  /// The second node connected by the spring
  unsigned node2;

  /// The rest length of the spring
  Real rest_len;
};

/// Class for deformable bodies simulated using systems of particles 
class PSDeformableBody : public DeformableBody 
{
  public:
    PSDeformableBody();
    virtual void integrate(Real t, Real h, boost::shared_ptr<Integrator<VectorN> > integrator);
    virtual void apply_impulse(const Vector3& j, const Vector3& p);
    virtual void calc_fwd_dyn(Real dt);
    virtual Real calc_mass() const { return _mass; }
    virtual void set_mesh(boost::shared_ptr<const IndexedTetraArray> tetra_mesh, boost::shared_ptr<Primitive> tri_mesh);
    const Spring& get_spring(unsigned i) { return _springs[i]; }
    void set_spring(unsigned i, Spring &s) { _springs[i] = s; }
    virtual void load_from_xml(XMLTreeConstPtr node, std::map<std::string, BasePtr>& id_map);
    virtual void save_to_xml(XMLTreePtr node, std::list<BaseConstPtr>& shared_objects) const;
    virtual void update_event_data(EventProblemData& epd);
    virtual void update_velocity(const EventProblemData& epd);
    virtual Real calc_potential_energy() const;

    /// The default spring stiffness constant
    Real default_KP;

    /// The default spring dampening constant
    Real default_KV;

  private:
    void determine_Dc_v(const std::vector<Event*>& contact_events, VectorN& Dc_v) const;
    void determine_Jc_v(const std::vector<Event*>& contact_events, VectorN& Jc_v) const;

    /// The set of springs
    std::vector<Spring> _springs;
}; // end class

} // end namespace

#endif

