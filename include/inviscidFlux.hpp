/*  This file is part of aither.
    Copyright (C) 2015-17  Michael Nucci (michael.nucci@gmail.com)

    Aither is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Aither is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef INVFLUXHEADERDEF  // only if the macro INVFLUXHEADERDEF is not defined
                          // execute these lines of code
#define INVFLUXHEADERDEF  // define the macro

#include <vector>        // vector
#include <string>        // string
#include <iostream>      // cout
#include <memory>        // unique_ptr
#include "vector3d.hpp"  // vector3d
#include "varArray.hpp"
#include "primitive.hpp"
#include "utility.hpp"
#include "eos.hpp"
#include "thermodynamic.hpp"
#include "turbulence.hpp"

using std::vector;
using std::string;
using std::cout;
using std::endl;
using std::cerr;
using std::ostream;
using std::unique_ptr;

// forward class declaration
class eos;
class thermodynamic;
class conserved;
class squareMatrix;
class turbModel;

class inviscidFlux : public varArray {
  // private member functions
  template <typename T>
  void ConstructFromPrim(const T &, const physics &phys,
                         const vector3d<double> &);

 public:
  // constructors
  inviscidFlux() : varArray() {}
  inviscidFlux(const int &numEqns, const int &numSpecies)
      : varArray(numEqns, numSpecies) {}
  template <typename T>
  inviscidFlux(const T &state, const physics &phys,
               const vector3d<double> &area)
      : inviscidFlux(state.Size(), state.NumSpecies()) {
    static_assert(std::is_same<primitive, T>::value ||
                      std::is_same<primitiveView, T>::value,
                  "T requires primitive or primativeView type");
    this->ConstructFromPrim(state, phys, area);
  }
  inviscidFlux(const conserved &cons, const physics &phys,
               const vector3d<double> &area)
      : inviscidFlux(cons.Size(), cons.NumSpecies()) {
    // convert conserved variables to primitive variables
    const primitive state(cons, phys);
    this->ConstructFromPrim(state, phys, area);
  }

  // move constructor and assignment operator
  inviscidFlux(inviscidFlux&&) noexcept = default;
  inviscidFlux& operator=(inviscidFlux&&) noexcept = default;

  // copy constructor and assignment operator
  inviscidFlux(const inviscidFlux&) = default;
  inviscidFlux& operator=(const inviscidFlux&) = default;

  // member functions
  const double & MassN(const int &ii) const { return this->SpeciesN(ii); }
  void RoeFlux(const inviscidFlux&, const varArray&);
  template <typename T1, typename T2>
  void AUSMFlux(const T1 &, const T2 &, const physics &phys,
                const vector3d<double> &, const double &, const double &,
                const double &, const double &, const double &);

  // destructor
  ~inviscidFlux() noexcept {}
};

// ----------------------------------------------------------------------------
// member functions

// flux is a 3D flux in the normal direction of the given face
/*

F = [rho * vel (dot) area
     rho * vel (dot) area * velx + P * areax
     rho * vel (dot) area * vely + P * areay
     rho * vel (dot) area * velz + P * areaz
     rho * vel (dot) area * H
     rho * vel (dot) area * k
     rho * vel (dot) area * w]

rho -- density
vel -- velocity vector (3D)
area -- area vector (3D)
P -- pressure
H -- enthalpy
velx, vely, velz -- velocity components
areax, areay, areaz -- area components
k -- turbulence kinetic energy
w -- specific turbulent dissipation

Constructor is put in a private member function because identical code is
used for constructing from primitive variables and conservative variables
once the conservative variables have been changed to primitive variables.
The C++11 way of delegating constructors is not used because the primitive
class is not fully defined in the inviscidFlux.hpp header. This way both
constructors (primitive version and conserved version) can call this function
to avoid code duplication.
*/
template <typename T>
void inviscidFlux::ConstructFromPrim(const T &state,
                                     const physics &phys,
                                     const vector3d<double> &normArea) {
  // state -- primitive variables
  // eqnState -- equation of state
  // normArea -- unit area vector of face
  static_assert(std::is_same<primitive, T>::value ||
                    std::is_same<primitiveView, T>::value,
                "T requires primitive or primativeView type");

  const auto vel = state.Velocity();
  const auto velNorm = vel.DotProd(normArea);

  for (auto ii = 0; ii < this->NumSpecies(); ++ii) {
    (*this)[ii] = state.RhoN(ii) * velNorm;
  }
  const auto rho = state.Rho();
  (*this)[this->MomentumXIndex()] =
      rho * velNorm * vel.X() + state.P() * normArea.X();
  (*this)[this->MomentumYIndex()] =
      rho * velNorm * vel.Y() + state.P() * normArea.Y();
  (*this)[this->MomentumZIndex()] =
      rho * velNorm * vel.Z() + state.P() * normArea.Z();
  (*this)[this->EnergyIndex()] =
      rho * velNorm * state.Enthalpy(phys);

  for (auto ii = 0; ii < this->NumTurbulence(); ++ii) {
    (*this)[this->TurbulenceIndex() + ii] =
        rho * velNorm * state.TurbulenceN(ii);
  }
}

template <typename T1, typename T2>
void inviscidFlux::AUSMFlux(const T1 &left, const T2 &right,
                            const physics &phys, const vector3d<double> &area,
                            const double &sos, const double &mPlusLBar,
                            const double &mMinusRBar, const double &pPlus,
                            const double &pMinus) {
  static_assert(std::is_same<primitive, T1>::value ||
                    std::is_same<primitiveView, T1>::value,
                "T1 requires primitive or primativeView type");
  static_assert(std::is_same<primitive, T2>::value ||
                    std::is_same<primitiveView, T2>::value,
                "T2 requires primitive or primativeView type");

  // calculate left flux
  const auto vl = mPlusLBar * sos;
  for (auto ii = 0; ii < this->NumSpecies(); ++ii) {
    (*this)[ii] = left.RhoN(ii) * vl;
  }

  // get indices
  auto imx = this->MomentumXIndex();
  auto imy = this->MomentumYIndex();
  auto imz = this->MomentumZIndex();
  auto ie = this->EnergyIndex();
  auto it = this->TurbulenceIndex();

  auto rhoL = left.Rho();
  (*this)[imx] = rhoL * vl * left.U() + pPlus * left.P() * area.X();
  (*this)[imy] = rhoL * vl * left.V() + pPlus * left.P() * area.Y();
  (*this)[imz] = rhoL * vl * left.W() + pPlus * left.P() * area.Z();
  (*this)[ie] = rhoL * vl * left.Enthalpy(phys);
  for (auto ii = 0; ii < this->NumTurbulence(); ++ii) {
    (*this)[it + ii] = rhoL * vl * left.TurbulenceN(ii);
  }

  // calculate right flux (add contribution)
  const auto vr = mMinusRBar * sos;
  for (auto ii = 0; ii < this->NumSpecies(); ++ii) {
    (*this)[ii] += right.RhoN(ii) * vr;
  }
  auto rhoR = right.Rho();
  (*this)[imx] += rhoR * vr * right.U() + pMinus * right.P() * area.X();
  (*this)[imy] += rhoR * vr * right.V() + pMinus * right.P() * area.Y();
  (*this)[imz] += rhoR * vr * right.W() + pMinus * right.P() * area.Z();
  (*this)[ie] += rhoR * vr * right.Enthalpy(phys);
  for (auto ii = 0; ii < this->NumTurbulence(); ++ii) {
    (*this)[it + ii] += rhoR * vr * right.TurbulenceN(ii);
  }
}


// ----------------------------------------------------------------------------
// function definitions
/* Function to calculate inviscid flux using Roe's approximate Riemann solver.
The function takes in the primitive varibles constructed
from the left and right states, an equation of state, a face area vector, and
outputs the inviscid flux as well as the maximum wave speed.
The function uses Harten's entropy fix to correct wave speeds near 0 and near
sonic.
__________________________________________
|         |       Ul|Ur        |         |
|         |         |          |         |
|   Ui-1  |   Ui    Ua  Ui+1   |   Ui+2  |
|         |         |          |         |
|         |         |          |         |
|_________|_________|__________|_________|

In the diagram above, Ul and Ur represent the reconstructed states, which for
the MUSCL scheme (1st and 2nd order) come from the stencil shown
above. Ua represents the average state at the face at which the flux will be
calculated. In this case it is a Roe average. Once the average
state has been calculated, the Roe flux can be calculated using the following
equation.

F = 1/2 * (Fl + Fr - D)

F represents the calculated Roe flux at the given face. Fl is the inviscid flux
calculated from the reconstructed state Ul. Fr is the inviscid
flux calculated from the reconstructed state Ur. D is the dissipation term which
is calculated using the Roe averaged state, as well as the
eigen values and eigen vectors resulting from the Roe linearization.

D = A * (Ur - Ul) = T * L * T^-1 * (Ur - Ul) = T * L * (Cr - Cl)

A is the linearized Roe matrix. It is equal to the convective flux jacobian
(dF/dU) calculated with the Roe averaged state. The linearization
essentially states that the flux jacobian (which is the change in flux over
change in state) mulitplied by the change in state is equal to the
change in flux. The change in flux is then added to the average of the physical
right and left fluxes (central difference). The Roe matrix, A,
can be diagonalized where T and T^-1 are the right and left eigenvectors
respectively and L is the eigenvalues. T^-1 multiplied by the change in
state results in the change in characteristic wave amplitude (Cr - Cl), or wave
strength. In its final form (right most) T represents the
characteristic waves, L represents the wave speeds, and (Cr - Cl) represents the
wave strength across the face.

*/
template <typename T1, typename T2>
inviscidFlux RoeFlux(const T1 &left, const T2 &right, const physics &phys,
                     const vector3d<double> &n) {
  // left -- primitive variables from left
  // right -- primitive variables from right
  // phys -- physics models
  // n -- norm area vector of face
  static_assert(std::is_same<primitive, T1>::value ||
                    std::is_same<primitiveView, T1>::value,
                "T1 requires primitive or primativeView type");
  static_assert(std::is_same<primitive, T2>::value ||
                    std::is_same<primitiveView, T2>::value,
                "T2 requires primitive or primativeView type");

  // compute Rho averaged quantities
  // Roe averaged state
  const auto roe = RoeAveragedState(left, right);
  // Roe averaged total enthalpy
  const auto hR = roe.Enthalpy(phys);
  // Roe averaged speed of sound
  const auto aR = roe.SoS(phys);
  // Roe averaged density
  const auto rhoR = roe.Rho();
  // Roe velocity dotted with normalized area vector
  const auto velNormR = roe.Velocity().DotProd(n);
  // Roe mass fractions
  const auto mfR = roe.MassFractions();
  // Delta between right and left states
  const auto delta = right - left;
  // normal velocity difference between left and right states
  const auto normVelDiff = delta.Velocity().DotProd(n);

  // calculate wave strengths (Cr - Cl)
  vector<double> waveStrength(4 + left.NumTurbulence());
  waveStrength[0] = (delta.P() - rhoR * aR * normVelDiff) / (2.0 * aR * aR);
  waveStrength[1] = delta.Rho() - delta.P() / (aR * aR);
  waveStrength[2] = (delta.P() + rhoR * aR * normVelDiff) / (2.0 * aR * aR);
  waveStrength[3] = rhoR;
  for (auto ii = 0; ii < left.NumTurbulence(); ++ii) {
    waveStrength[4 + ii] = rhoR * delta.TurbulenceN(ii) +
                           roe.TurbulenceN(ii) * delta.Rho() -
                           delta.P() * roe.TurbulenceN(ii) / (aR * aR);
  }

  // calculate absolute value of wave speeds (L)
  vector<double> waveSpeed(4 + left.NumTurbulence());
  waveSpeed[0] = fabs(velNormR - aR);  // left moving acoustic wave speed
  waveSpeed[1] = fabs(velNormR);       // entropy wave speed
  waveSpeed[2] = fabs(velNormR + aR);  // right moving acoustic wave speed
  waveSpeed[3] = fabs(velNormR);       // shear wave speed
  for (auto ii = 0; ii < left.NumTurbulence(); ++ii) {
    waveSpeed[4 + ii] = fabs(velNormR);  // turbulent eqn wave speed
  }

  // calculate entropy fix (Harten) and adjust wave speeds if necessary
  // default setting for entropy fix to kick in
  constexpr auto entropyFix = 0.1;

  if (waveSpeed[0] < entropyFix) {
    waveSpeed[0] =
        0.5 * (waveSpeed[0] * waveSpeed[0] / entropyFix + entropyFix);
  }
  if (waveSpeed[2] < entropyFix) {
    waveSpeed[2] =
        0.5 * (waveSpeed[2] * waveSpeed[2] / entropyFix + entropyFix);
  }

  // calculate right eigenvectors (T)
  // get indices
  const auto imx = left.MomentumXIndex();
  const auto imy = left.MomentumYIndex();
  const auto imz = left.MomentumZIndex();
  const auto ie = left.EnergyIndex();
  const auto it = left.TurbulenceIndex();

  // calculate eigenvector due to left acoustic wave
  varArray lAcousticEigV(left.Size(), left.NumSpecies());
  for (auto ii = 0; ii < lAcousticEigV.NumSpecies(); ++ii) {
    lAcousticEigV[ii] = mfR[ii];
  }
  lAcousticEigV[imx] = roe.U() - aR * n.X();
  lAcousticEigV[imy] = roe.V() - aR * n.Y();
  lAcousticEigV[imz] = roe.W() - aR * n.Z();
  lAcousticEigV[ie] = hR - aR * velNormR;
  for (auto ii = 0; ii < lAcousticEigV.NumTurbulence(); ++ii) {
    lAcousticEigV[it + ii] = roe.TurbulenceN(ii);
  }

  // calculate eigenvector due to entropy wave
  varArray entropyEigV(left.Size(), left.NumSpecies());
  for (auto ii = 0; ii < entropyEigV.NumSpecies(); ++ii) {
    entropyEigV[ii] = 1.0;
  }
  // non-species values are repated for number of species
  entropyEigV[imx] = roe.U() * entropyEigV.NumSpecies();
  entropyEigV[imy] = roe.V() * entropyEigV.NumSpecies();
  entropyEigV[imz] = roe.W() * entropyEigV.NumSpecies();
  entropyEigV[ie] = 0.5 * roe.Velocity().MagSq() * entropyEigV.NumSpecies();
  // turbulence values are zero

  // calculate eigenvector due to right acoustic wave
  varArray rAcousticEigV(left.Size(), left.NumSpecies());
  for (auto ii = 0; ii < rAcousticEigV.NumSpecies(); ++ii) {
    rAcousticEigV[ii] = mfR[ii];
  }
  rAcousticEigV[imx] = roe.U() + aR * n.X();
  rAcousticEigV[imy] = roe.V() + aR * n.Y();
  rAcousticEigV[imz] = roe.W() + aR * n.Z();
  rAcousticEigV[ie] = hR + aR * velNormR;
  for (auto ii = 0; ii < rAcousticEigV.NumTurbulence(); ++ii) {
    rAcousticEigV[it + ii] = roe.TurbulenceN(ii);
  }

  // calculate eigenvector due to shear wave
  varArray shearEigV(left.Size(), left.NumSpecies());
  // species values are zero
  shearEigV[imx] = delta.U() - normVelDiff * n.X();
  shearEigV[imy] = delta.V() - normVelDiff * n.Y();
  shearEigV[imz] = delta.W() - normVelDiff * n.Z();
  shearEigV[ie] =
      roe.Velocity().DotProd(delta.Velocity()) - velNormR * normVelDiff;
  // turbulence values are zero

  // calculate eigenvector due to turbulent equation 1
  varArray tkeEigV(left.Size(), left.NumSpecies());
  if (tkeEigV.HasTurbulenceData()) {
    tkeEigV[it] = 1.0;
  }

  // calculate eigenvector due to turbulent equation 2
  varArray omgEigV(left.Size(), left.NumSpecies());
  if (omgEigV.HasTurbulenceData() && omgEigV.NumTurbulence() > 1) {
    omgEigV[it + 1] = 1.0;
  }

  // calculate dissipation term ( eigenvector * wave speed * wave strength)
  varArray dissipation(left.Size(), left.NumSpecies());
  for (auto ii = 0; ii < dissipation.Size(); ++ii) {
    // contribution from left acoustic wave
    // contribution from entropy wave
    // contribution from right acoustic wave
    // contribution from shear wave
    // contribution from turbulent wave 1
    // contribution from turbulent wave 2
    dissipation[ii] = waveSpeed[0] * waveStrength[0] * lAcousticEigV[ii] +
                      waveSpeed[1] * waveStrength[1] * entropyEigV[ii] +
                      waveSpeed[2] * waveStrength[2] * rAcousticEigV[ii] +
                      waveSpeed[3] * waveStrength[3] * shearEigV[ii];
    if (dissipation.HasTurbulenceData()) {
      dissipation[ii] += waveSpeed[4] * waveStrength[4] * tkeEigV[ii] +
                         waveSpeed[5] * waveStrength[5] * omgEigV[ii];
    }
  }

  // calculate left/right physical flux
  inviscidFlux leftFlux(left, phys, n);
  inviscidFlux rightFlux(right, phys, n);

  // calculate numerical Roe flux
  leftFlux.RoeFlux(rightFlux, dissipation);

  return leftFlux;
}

/* Function to calculate the AUSMPW+ flux as described in "Accurate Compuations
   of Hypersonic Flows Using AUSMPW+ Scheme and Shock-Aligned Grid Technique". 
   by Kim, Kim, & Rho. AIAA 1998. Unlike the Roe scheme, this flux scheme is an
   example of flux vector splitting instead of flux difference splitting. 
   Although more dissipative, it has shown good performance in the high speed
   regime where the Roe scheme can suffer from carbuncle problems. The flux is
   split into the convective and pressure terms, which are then split based on
   the mach number and pressure. The flux discretization is shown below.

   F = M^+_l * c * F_cl + M^-_r * c * F_cr + P^+_l * P_l + P^-_r * P_r
*/
template <typename T1, typename T2>
inviscidFlux AUSMFlux(const T1 &left, const T2 &right, const physics &phys,
                      const vector3d<double> &area) {
  // left -- primitive variables from left
  // right -- primitive variables from right
  // phys -- physics models
  // area -- norm area vector of face
  static_assert(std::is_same<primitive, T1>::value ||
                    std::is_same<primitiveView, T1>::value,
                "T1 requires primitive or primativeView type");
  static_assert(std::is_same<primitive, T2>::value ||
                    std::is_same<primitiveView, T2>::value,
                "T2 requires primitive or primativeView type");

  // calculate average specific enthalpy on face
  const auto tl = left.Temperature(phys.EoS());
  const auto tr = right.Temperature(phys.EoS());
  const auto mfl = left.MassFractions();
  const auto mfr = right.MassFractions();
  const auto hl = phys.Thermodynamic()->SpecEnthalpy(tl, mfl);
  const auto hr = phys.Thermodynamic()->SpecEnthalpy(tr, mfr);
  const auto h = 0.5 * (hl + hr);

  // calculate c* from Kim, Kim, Rho 1998
  vector<double> mf(mfl.size());
  for (auto ii = 0U; ii < mf.size(); ++ii) {
    mf[ii] = 0.5 * (mfl[ii] + mfr[ii]);
  }
  const auto t = 0.5 * (tl + tr);
  const auto gamma = phys.Thermodynamic()->Gamma(t, mf);
  const auto sosStar = sqrt(2.0 * h * (gamma - 1.0) / (gamma + 1.0));

  // calculate left/right mach numbers
  const auto vell = left.Velocity().DotProd(area);
  const auto velr = right.Velocity().DotProd(area);
  const auto ml = vell / sosStar;
  const auto mr = velr / sosStar;

  // calculate speed of sound on face c_1/2 from Kim, Kim, Rho 1998
  const auto vel = 0.5 * (vell + velr);
  auto sos = 0.0;
  if (vel < 0.0) {
    sos = sosStar * sosStar / std::max(vell, sosStar);
  } else if (vel > 0.0) {
    sos = sosStar * sosStar / std::max(velr, sosStar);
  }

  // calculate split mach number and pressure terms
  const auto mPlusL =
      fabs(ml) <= 1.0 ? 0.25 * pow(ml + 1.0, 2.0) : 0.5 * (ml + fabs(ml));
  const auto mMinusR =
      fabs(mr) <= 1.0 ? -0.25 * pow(mr - 1.0, 2.0) : 0.5 * (mr - fabs(mr));
  const auto pPlus = fabs(ml) <= 1.0 ? 0.25 * pow(ml + 1.0, 2.0) * (2.0 - ml)
                                     : 0.5 * (1.0 + Sign(ml));
  const auto pMinus = fabs(mr) <= 1.0 ? 0.25 * pow(mr - 1.0, 2.0) * (2.0 + mr)
                                      : 0.5 * (1.0 - Sign(mr));

  // calculate pressure weighting terms
  const auto ps = pPlus * left.P() + pMinus * right.P();
  const auto w =
      1.0 - pow(std::min(left.P() / right.P(), right.P() / left.P()), 3.0);
  const auto fl = fabs(ml) < 1.0 ? left.P() / ps - 1.0 : 0.0;
  const auto fr = fabs(mr) < 1.0 ? right.P() / ps - 1.0 : 0.0;

  // calculate final split properties
  const auto mavg = mPlusL + mMinusR;
  const auto mPlusLBar = mavg >= 0.0
                             ? mPlusL + mMinusR * ((1.0 - w) * (1.0 + fr) - fl)
                             : mPlusL * w * (1.0 + fl);
  const auto mMinusRBar =
      mavg >= 0.0 ? mMinusR * w * (1.0 + fr)
                  : mMinusR + mPlusL * ((1.0 - w) * (1.0 + fl) - fr);

  inviscidFlux ausm(left.Size(), left.NumSpecies());
  ausm.AUSMFlux(left, right, phys, area, sos, mPlusLBar, mMinusRBar, pPlus,
                pMinus);
  return ausm;
}

template <typename T1, typename T2>
inviscidFlux InviscidFlux(const T1 &left, const T2 &right,
                          const physics &phys,
                          const vector3d<double> &area, const string &flux) {
  static_assert(std::is_same<primitive, T1>::value ||
                    std::is_same<primitiveView, T1>::value,
                "T1 requires primitive or primativeView type");
  static_assert(std::is_same<primitive, T2>::value ||
                    std::is_same<primitiveView, T2>::value,
                "T2 requires primitive or primativeView type");
  
  inviscidFlux invFlux;
  if (flux == "roe") {
    invFlux = RoeFlux(left, right, phys, area);
  } else if (flux == "ausm") {
    invFlux = AUSMFlux(left, right, phys, area);
  } else {
    cerr << "ERROR: inviscid flux type " << flux << " is not recognized!"
         << endl;
    cerr << "Choose 'roe' or 'ausm'" << endl;
    exit(EXIT_FAILURE);
  }
  return invFlux;
}

template <typename T1, typename T2>
inviscidFlux RusanovFlux(const T1 &left, const T2 &right,
                         const physics &phys,
                         const vector3d<double> &areaNorm,
                         const bool &positive) {
  // left -- primitive variables from left
  // right -- primitive variables from right
  // phys -- physics models
  // areaNorm -- norm area vector of face
  // positive -- flag that is positive to add spectral radius
  static_assert(std::is_same<primitive, T1>::value ||
                    std::is_same<primitiveView, T1>::value,
                "T1 requires primitive or primativeView type");
  static_assert(std::is_same<primitive, T2>::value ||
                    std::is_same<primitiveView, T2>::value,
                "T2 requires primitive or primativeView type");

  // calculate maximum spectral radius
  const auto leftSpecRad =
      fabs(left.Velocity().DotProd(areaNorm)) + left.SoS(phys);
  const auto rightSpecRad =
      fabs(right.Velocity().DotProd(areaNorm)) + right.SoS(phys);
  const auto fac = positive ? -1.0 : 1.0;
  const auto specRad = fac * std::max(leftSpecRad, rightSpecRad);

  // calculate left/right physical flux
  inviscidFlux leftFlux(left, phys, areaNorm);
  inviscidFlux rightFlux(right, phys, areaNorm);

  return 0.5 * (leftFlux + rightFlux - specRad);
}

// function to take in the primitive variables, equation of state, face area
// vector, and primitive variable update and calculate the change in the
// convective flux
template <typename T1, typename T2>
inviscidFlux ConvectiveFluxUpdate(const T1 &state,
                                  const T2 &stateUpdate,
                                  const physics &phys,
                                  const vector3d<double> &normArea) {
  static_assert(std::is_same<primitive, T1>::value ||
                    std::is_same<primitiveView, T1>::value,
                "T1 requires primitive or primativeView type");
  static_assert(std::is_same<primitive, T2>::value ||
                    std::is_same<primitiveView, T2>::value,
                "T2 requires primitive or primativeView type");

  // get inviscid flux of old state
  const inviscidFlux oldFlux(state, phys, normArea);
  // get updated inviscid flux
  const inviscidFlux newFlux(stateUpdate, phys, normArea);

  // calculate difference in flux
  return newFlux - oldFlux;
}

#endif
