#define BIORBD_API_EXPORTS
#include "RigidBody/Joints.h"

#include <rbdl/rbdl_utils.h>
#include <rbdl/Kinematics.h>
#include <rbdl/Dynamics.h>
#include "BiorbdModel.h"
#include "Utils/Error.h"
#include "Utils/Matrix.h"
#include "Utils/Matrix3d.h"
#include "Utils/Quaternion.h"
#include "Utils/RotoTrans.h"
#include "Utils/Rotation.h"
#include "Utils/Scalar.h"
#include "Utils/SpatialVector.h"
#include "Utils/String.h"

#include "RigidBody/ExternalForceSet.h"
#include "RigidBody/GeneralizedCoordinates.h"
#include "RigidBody/GeneralizedVelocity.h"
#include "RigidBody/GeneralizedAcceleration.h"
#include "RigidBody/GeneralizedTorque.h"
#include "RigidBody/Segment.h"
#include "RigidBody/Markers.h"
#include "RigidBody/NodeSegment.h"
#include "RigidBody/MeshFace.h"
#include "RigidBody/Mesh.h"
#include "RigidBody/SegmentCharacteristics.h"
#include "RigidBody/Contacts.h"
#include "RigidBody/SoftContacts.h"


using namespace BIORBD_NAMESPACE;

rigidbody::Joints::Joints() :
    RigidBodyDynamics::Model(),
    m_segments(std::make_shared<std::vector<rigidbody::Segment>>()),
    m_nbRoot(std::make_shared<size_t>(0)),
    m_nbDof(std::make_shared<size_t>(0)),
    m_nbQ(std::make_shared<size_t>(0)),
    m_nbQdot(std::make_shared<size_t>(0)),
    m_nbQddot(std::make_shared<size_t>(0)),
    m_nRotAQuat(std::make_shared<size_t>(0)),
    m_isKinematicsComputed(std::make_shared<bool>(false)),
    m_totalMass(std::make_shared<utils::Scalar>(0))
{
    // Redefining gravity so it is on z by default
    this->gravity = utils::Vector3d (0, 0, -9.81);
}

rigidbody::Joints::Joints(const rigidbody::Joints &other) :
    RigidBodyDynamics::Model(other),
    m_segments(other.m_segments),
    m_nbRoot(other.m_nbRoot),
    m_nbDof(other.m_nbDof),
    m_nbQ(other.m_nbQ),
    m_nbQdot(other.m_nbQdot),
    m_nbQddot(other.m_nbQddot),
    m_nRotAQuat(other.m_nRotAQuat),
    m_isKinematicsComputed(other.m_isKinematicsComputed),
    m_totalMass(other.m_totalMass)
{

}

rigidbody::Joints::~Joints()
{

}

rigidbody::Joints rigidbody::Joints::DeepCopy() const
{
    rigidbody::Joints copy;
    copy.DeepCopy(*this);
    return copy;
}

void rigidbody::Joints::DeepCopy(const rigidbody::Joints &other)
{
    static_cast<RigidBodyDynamics::Model&>(*this) = other;
    m_segments->resize(other.m_segments->size());
    for (size_t i=0; i<other.m_segments->size(); ++i) {
        (*m_segments)[i] = (*other.m_segments)[i].DeepCopy();
    }
    *m_nbRoot = *other.m_nbRoot;
    *m_nbDof = *other.m_nbDof;
    *m_nbQ = *other.m_nbQ;
    *m_nbQdot = *other.m_nbQdot;
    *m_nbQddot = *other.m_nbQddot;
    *m_nRotAQuat = *other.m_nRotAQuat;
    *m_isKinematicsComputed = *other.m_isKinematicsComputed;
    *m_totalMass = *other.m_totalMass;
}

size_t rigidbody::Joints::nbGeneralizedTorque() const
{
    return nbQddot();
}
size_t rigidbody::Joints::nbDof() const
{
    return *m_nbDof;
}

std::vector<utils::String> rigidbody::Joints::nameDof() const
{
    std::vector<utils::String> names;
    for (size_t i = 0; i < nbSegment(); ++i) {
        for (size_t j = 0; j < segment(i).nbDof(); ++j) {
            names.push_back(segment(i).name() + "_" + segment(i).nameDof(j));
        }
    }
    // Append Quaternion Q
    for (size_t i = 0; i < nbSegment(); ++i) {
        if (segment(i).isRotationAQuaternion()) {
            names.push_back(segment(i).name() + "_" + segment(i).nameDof(3));
        }
    }
    return names;
}

size_t rigidbody::Joints::nbQ() const
{
    return *m_nbQ;
}
size_t rigidbody::Joints::nbQdot() const
{
    return *m_nbQdot;
}
size_t rigidbody::Joints::nbQddot() const
{
    return *m_nbQddot;
}
size_t rigidbody::Joints::nbRoot() const
{
    return *m_nbRoot;
}

utils::Scalar rigidbody::Joints::mass() const
{
    return *m_totalMass;
}

size_t rigidbody::Joints::AddSegment(
    const utils::String &segmentName,
    const utils::String &parentName,
    const utils::String &translationSequence,
    const utils::String &rotationSequence,
    const std::vector<utils::Range>& QRanges,
    const std::vector<utils::Range>& QDotRanges,
    const std::vector<utils::Range>& QDDotRanges,
    const rigidbody::SegmentCharacteristics& characteristics,
    const utils::RotoTrans& referenceFrame)
{
    rigidbody::Segment tp(
        *this, segmentName, parentName, translationSequence,
        rotationSequence, QRanges, QDotRanges, QDDotRanges, characteristics,
        RigidBodyDynamics::Math::SpatialTransform(referenceFrame.rot().transpose(),
                referenceFrame.trans())
    );
    if (this->GetBodyId(parentName.c_str()) == std::numeric_limits<unsigned int>::max()) {
        *m_nbRoot +=
            tp.nbDof();    // If the segment name is "Root", add the number of DoF of root
    }
    *m_nbDof += tp.nbDof();
    *m_nbQ += tp.nbQ();
    *m_nbQdot += tp.nbQdot();
    *m_nbQddot += tp.nbQddot();

    if (tp.isRotationAQuaternion()) {
        ++*m_nRotAQuat;
    }

    *m_totalMass +=
        characteristics.mMass; // Add the segment mass to the total body mass
    m_segments->push_back(tp);
    return 0;
}
size_t rigidbody::Joints::AddSegment(
    const utils::String &segmentName,
    const utils::String &parentName,
    const utils::String &seqR,
    const std::vector<utils::Range>& QRanges,
    const std::vector<utils::Range>& QDotRanges,
    const std::vector<utils::Range>& QDDotRanges,
    const rigidbody::SegmentCharacteristics& characteristics,
    const utils::RotoTrans& referenceFrame)
{
    rigidbody::Segment tp(
        *this, segmentName, parentName, seqR, QRanges, QDotRanges, QDDotRanges,
        characteristics, RigidBodyDynamics::Math::SpatialTransform(
            referenceFrame.rot().transpose(), referenceFrame.trans())
    );
    if (this->GetBodyId(parentName.c_str()) == std::numeric_limits<unsigned int>::max()) {
        *m_nbRoot +=
            tp.nbDof();    //  If the name of the segment is "Root", add the number of DoF of root
    }
    *m_nbDof += tp.nbDof();

    *m_totalMass +=
        characteristics.mMass; // Add the segment mass to the total body mass
    m_segments->push_back(tp);
    return 0;
}

utils::Vector3d rigidbody::Joints::getGravity() const
{
    return gravity;
}

void rigidbody::Joints::setGravity(
    const utils::Vector3d &newGravity)
{
    gravity = newGravity;
}

void rigidbody::Joints::updateSegmentCharacteristics(
    size_t idx,
    const rigidbody::SegmentCharacteristics& characteristics)
{
    utils::Error::check(idx < m_segments->size(),
                                "Asked for a wrong segment (out of range)");
    (*m_segments)[idx].updateCharacteristics(*this, characteristics);
}

const rigidbody::Segment& rigidbody::Joints::segment(
    size_t idx) const
{
    utils::Error::check(idx < m_segments->size(),
                                "Asked for a wrong segment (out of range)");
    return (*m_segments)[idx];
}

const rigidbody::Segment &rigidbody::Joints::segment(
    const utils::String & name) const
{
    return segment(static_cast<size_t>(getBodyBiorbdId(name.c_str())));
}

const std::vector<rigidbody::Segment>& rigidbody::Joints::segments() const
{
    return *m_segments;
}

size_t rigidbody::Joints::nbSegment() const
{
    return m_segments->size();
}

int rigidbody::Joints::getBodyBiorbdId(
        const utils::String &segmentName) const
{
    for (size_t i=0; i<m_segments->size(); ++i)
        if (!(*m_segments)[i].name().compare(segmentName)) {
            return static_cast<int>(i);
        }
    return -1;
}


int rigidbody::Joints::getBodyRbdlId(
        const utils::String &segmentName) const
{
    return GetBodyId(segmentName.c_str());
}

int rigidbody::Joints::getBodyRbdlIdToBiorbdId(
        const int idx) const
{
    // Assuming that this is also a joint type (via BiorbdModel)
    const rigidbody::Joints &model = dynamic_cast<const rigidbody::Joints &>(*this);
    std::string bodyName = model.GetBodyName(idx);
    return model.getBodyBiorbdId(bodyName);
}

size_t rigidbody::Joints::getBodyBiorbdIdToRbdlId(
        const int idx) const
{
    return (*m_segments)[idx].id();
}

std::vector<std::vector<size_t> > rigidbody::Joints::getDofSubTrees()
{
    // initialize subTrees
    std::vector<std::vector<size_t> > subTrees;
    std::vector<size_t> subTree_empty;
    for (size_t j=0; j<this->mu.size(); ++j) {
        subTrees.push_back(subTree_empty);
    }

    // Get all dof without parent
    std::vector<size_t> dof_with_no_parent_id;
    for (size_t i=1; i<this->mu.size(); ++i) { // begin at 1 because 0 is its own parent in rbdl.
      if (this->lambda[i]==0) {
          dof_with_no_parent_id.push_back(i);
      }
    }

    // Get all subtrees of dofs without parents
    for (size_t i=0; i<dof_with_no_parent_id.size(); ++i) {
        size_t dof_id = dof_with_no_parent_id[i];

        // initialize subTrees_temp
        std::vector<std::vector<size_t> > subTrees_temp;
        for (size_t j=0; j<this->mu.size(); ++j) {
          subTrees_temp.push_back(subTree_empty);
        }

        std::vector<std::vector<size_t> > subTrees_temp_filled = recursiveDofSubTrees(subTrees_temp, dof_id);
        for (size_t j=0; j<subTrees_temp.size(); ++j) {
            if (subTrees_temp_filled[j].empty()) {
                continue;
            }
            else
            {
                subTrees[j].insert(subTrees[j].end(),
                                     subTrees_temp_filled[j].begin(),
                                     subTrees_temp_filled[j].end());
            }
        }

    }

    subTrees.erase(subTrees.begin());

    return  subTrees;
}

std::vector<std::vector<size_t> > rigidbody::Joints::recursiveDofSubTrees(
        std::vector<std::vector<size_t> >subTrees,
        size_t idx)
{
    size_t q_index_i = this->mJoints[idx].q_index;
    subTrees[idx].push_back(q_index_i);

    std::vector<std::vector<size_t> > subTrees_filled;
    subTrees_filled = subTrees;

    std::vector<unsigned int> child_idx = this->mu[idx];

    if (child_idx.size() > 0){
       for (size_t i=0; i<child_idx.size(); ++i) {
            size_t cur_child_id = child_idx[i];
            subTrees_filled = recursiveDofSubTrees(subTrees_filled, cur_child_id);
            std::vector<size_t> subTree_child = subTrees_filled[cur_child_id];

            subTrees_filled[idx].insert(subTrees_filled[idx].end(),
                                 subTree_child.begin(),
                                 subTree_child.end());
      }
    }

    return subTrees_filled;
}


std::vector<utils::RotoTrans> rigidbody::Joints::allGlobalJCS(
    const rigidbody::GeneralizedCoordinates &Q, 
    bool updateKin
)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) UpdateKinematicsCustom (&Q, nullptr, nullptr);
    return allGlobalJCS();
}

std::vector<utils::RotoTrans> rigidbody::Joints::allGlobalJCS()
const
{
    std::vector<utils::RotoTrans> out;
    for (size_t i=0; i<m_segments->size(); ++i) {
        out.push_back(globalJCS(i));
    }
    return out;
}

utils::RotoTrans rigidbody::Joints::globalJCS(
    const rigidbody::GeneralizedCoordinates &Q,
    const utils::String &name)
{
    UpdateKinematicsCustom (&Q, nullptr, nullptr);
    return globalJCS(name);
}

utils::RotoTrans rigidbody::Joints::globalJCS(
    const rigidbody::GeneralizedCoordinates &Q,
    size_t idx)
{
    // update the Kinematics if necessary
    UpdateKinematicsCustom (&Q, nullptr, nullptr);
    return globalJCS(idx);
}

utils::RotoTrans rigidbody::Joints::globalJCS(
    const utils::String &name) const
{
    return globalJCS(static_cast<size_t>(getBodyBiorbdId(name)));
}

utils::RotoTrans rigidbody::Joints::globalJCS(
    size_t idx) const
{
    return CalcBodyWorldTransformation((*m_segments)[idx].id());
}

std::vector<utils::RotoTrans> rigidbody::Joints::localJCS()
const
{
    std::vector<utils::RotoTrans> out;

    for (size_t i=0; i<m_segments->size(); ++i) {
        out.push_back(localJCS(i));
    }

    return out;
}
utils::RotoTrans rigidbody::Joints::localJCS(
    const utils::String &name) const
{
    return localJCS(static_cast<size_t>(getBodyBiorbdId(name.c_str())));
}
utils::RotoTrans rigidbody::Joints::localJCS(
    const size_t idx) const
{
    return (*m_segments)[idx].localJCS();
}


std::vector<rigidbody::NodeSegment>
rigidbody::Joints::projectPoint(
    const rigidbody::GeneralizedCoordinates& Q,
    const std::vector<rigidbody::NodeSegment>& v,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) {
        UpdateKinematicsCustom(&Q, nullptr, nullptr);
    }
    updateKin = false;

    // Assuming that this is also a marker type (via BiorbdModel)
    const rigidbody::Markers& marks =
        dynamic_cast<rigidbody::Markers&>(*this);

    // Sécurité
    utils::Error::check(marks.nbMarkers() == v.size(),
                                "Number of marker must be equal to number of Vector3d");

    std::vector<rigidbody::NodeSegment> out;
    for (size_t i = 0; i < marks.nbMarkers(); ++i) {
        rigidbody::NodeSegment tp(marks.marker(i));
        if (tp.nbAxesToRemove() != 0) {
            tp = v[i].applyRT(globalJCS(tp.parent()).transpose());
            // Prendre la position du nouveau marker avec les infos de celui du modèle
            out.push_back(projectPoint(Q, tp, updateKin));
        } else
            // S'il ne faut rien retirer (renvoyer tout de suite la même position)
        {
            out.push_back(v[i]);
        }
    }
    return out;
}

rigidbody::NodeSegment rigidbody::Joints::projectPoint(
    const rigidbody::GeneralizedCoordinates &Q,
    const utils::Vector3d &v,
    int segmentIdx,
    const utils::String& axesToRemove,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) {
        UpdateKinematicsCustom (&Q);
    }

    // Create a marker
    const utils::String& segmentName(segment(static_cast<size_t>(segmentIdx)).name());
    rigidbody::NodeSegment node( v.
        applyRT(globalJCS(static_cast<size_t>(segmentIdx)).transpose()), 
        "tp",
        segmentName,
        true, 
        true, 
        axesToRemove, 
        static_cast<int>(GetBodyId(segmentName.c_str()))
    );

    // Project and then reset in global
    return projectPoint(Q, node, false);
}

rigidbody::NodeSegment rigidbody::Joints::projectPoint(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::NodeSegment &n,
    bool updateKin)
{
    // Assuming that this is also a Marker type (via BiorbdModel)
    return dynamic_cast<rigidbody::Markers &>(*this).marker(Q, n, true,
            updateKin);
}

utils::Matrix rigidbody::Joints::projectPointJacobian(
    const rigidbody::GeneralizedCoordinates &Q,
    rigidbody::NodeSegment node,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) {
        UpdateKinematicsCustom (&Q);
    }
    updateKin = false;

    // Assuming that this is also a Marker type (via BiorbdModel)
    rigidbody::Markers &marks = dynamic_cast<rigidbody::Markers &>
                                        (*this);

    // If the point has not been projected, there is no effect
    if (node.nbAxesToRemove() != 0) {
        // Jacobian of the marker
        node.applyRT(globalJCS(node.parent()).transpose());
        utils::Matrix G_tp(marks.markersJacobian(Q, node.parent(),
                                   utils::Vector3d(0,0,0), updateKin));
        utils::Matrix JCor(utils::Matrix::Zero(9, static_cast<unsigned int>(nbQ())));
        CalcMatRotJacobian(Q, GetBodyId(node.parent().c_str()),
                           utils::Matrix3d::Identity(), JCor, updateKin);
        for (size_t n=0; n<3; ++n)
            if (node.isAxisKept(n)) {
                G_tp += JCor.block(static_cast<unsigned int>(n)*3,0,3, static_cast<unsigned int>(nbQ())) * node(static_cast<unsigned int>(n));
            }

        return G_tp;
    } else {
        // Return the value
        return utils::Matrix::Zero(3, static_cast<unsigned int>(nbQ()));
    }
}

utils::Matrix rigidbody::Joints::projectPointJacobian(
    const rigidbody::GeneralizedCoordinates &Q,
    const utils::Vector3d &v,
    int segmentIdx,
    const utils::String& axesToRemove,
    bool updateKin)
{
    // Find the point
    const rigidbody::NodeSegment& p(projectPoint(Q, v, segmentIdx,
                                            axesToRemove, updateKin));

    // Return the value
    return projectPointJacobian(Q, p, updateKin);
}

std::vector<utils::Matrix>
rigidbody::Joints::projectPointJacobian(
    const rigidbody::GeneralizedCoordinates &Q,
    const std::vector<rigidbody::NodeSegment> &v,
    bool updateKin)
{
    // Gather the points
    const std::vector<rigidbody::NodeSegment>& tp(projectPoint(Q, v,
            updateKin));

    // Calculate the Jacobian if the point is not projected
    std::vector<utils::Matrix> G;

    for (size_t i=0; i<tp.size(); ++i) {
        // Actual marker
        G.push_back(projectPointJacobian(Q, rigidbody::NodeSegment(v[i]), false));
    }
    return G;
}

RigidBodyDynamics::Math::SpatialTransform
rigidbody::Joints::CalcBodyWorldTransformation (
    const rigidbody::GeneralizedCoordinates &Q,
    const size_t segmentIdx,
    bool updateKin)
{
    // update the Kinematics if necessary
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) {
        UpdateKinematicsCustom (&Q);
    }
    return CalcBodyWorldTransformation(segmentIdx);
}

RigidBodyDynamics::Math::SpatialTransform
rigidbody::Joints::CalcBodyWorldTransformation(
    const size_t segmentIdx) const
{
    if (segmentIdx >= this->fixed_body_discriminator) {
        size_t fbody_id = segmentIdx - static_cast<size_t>(this->fixed_body_discriminator);
        size_t parent_id = static_cast<size_t>(this->mFixedBodies[fbody_id].mMovableParent);
        utils::RotoTrans parentRT(
            this->X_base[parent_id].E.transpose(),
            this->X_base[parent_id].r);
        utils::RotoTransNode bodyRT(
            utils::RotoTrans(
                this->mFixedBodies[fbody_id].mParentTransform.E.transpose(),
                this->mFixedBodies[fbody_id].mParentTransform.r)
            , "", "");
        const utils::RotoTrans& transfo_tp = parentRT * bodyRT;
        return RigidBodyDynamics::Math::SpatialTransform (transfo_tp.rot(),
                transfo_tp.trans());
    }

    return RigidBodyDynamics::Math::SpatialTransform (
               this->X_base[segmentIdx].E.transpose(), this->X_base[segmentIdx].r);
}


// Get a segment's angular velocity
utils::Vector3d rigidbody::Joints::segmentAngularVelocity(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    size_t idx,
    bool updateKin)
{
    // Assuming that this is also a joint type (via BiorbdModel)
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif

    const utils::String& segmentName(segment(idx).name());
    size_t id(static_cast<size_t>(this->GetBodyId(segmentName.c_str())));

    // Calculate the velocity of the point
    return RigidBodyDynamics::CalcPointVelocity6D(
                *this, Q, Qdot, static_cast<unsigned int>(id), utils::Vector3d(0, 0, 0), updateKin).block(0, 0, 3, 1);
}

utils::Vector3d rigidbody::Joints::CoM(
    const rigidbody::GeneralizedCoordinates &Q,
    bool updateKin)
{
    // For each segment, find the CoM (CoM = sum(segment_mass * pos_com_seg) / total mass)
    const std::vector<rigidbody::NodeSegment>& com_segment(CoMbySegment(Q,
            updateKin));
    utils::Vector3d com(0, 0, 0);
    for (size_t i=0; i<com_segment.size(); ++i) {
        com += (*m_segments)[i].characteristics().mMass * com_segment[i];
    }

    // Divide by total mass
    com = com/this->mass();

    // Return the CoM
    return com;
}

utils::Vector3d rigidbody::Joints::angularMomentum(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    bool updateKin)
{
    return CalcAngularMomentum(Q, Qdot, updateKin);
}

utils::Matrix rigidbody::Joints::massMatrix (
    const rigidbody::GeneralizedCoordinates &Q,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    RigidBodyDynamics::Math::MatrixNd massMatrix(static_cast<unsigned int>(nbQ()), static_cast<unsigned int>(nbQ()));
    massMatrix.setZero();
    RigidBodyDynamics::CompositeRigidBodyAlgorithm(*this, Q, massMatrix, updateKin);
    return massMatrix;
}

utils::Matrix rigidbody::Joints::massMatrixInverse (
    const rigidbody::GeneralizedCoordinates &Q,
    bool updateKin)
{
    int i = 0; // for loop purpose
    int j = 0; // for loop purpose
    RigidBodyDynamics::Math::MatrixNd Minv(this->dof_count, this->dof_count);
    Minv.setZero();

#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) {
        UpdateKinematicsCustom(&Q, nullptr, nullptr);
    }
    // First Forward Pass
    for (i = 1; i < this->mBodies.size(); i++) {

      this->I[i].setSpatialMatrix(this->IA[i]);
      }
    // End First Forward Pass

    // set F (n x 6 x n)
    RigidBodyDynamics::Math::MatrixNd F_i(6, this->dof_count);
    F_i.setZero();
    // Fill a vector of matrix (6 x n)
    std::vector<RigidBodyDynamics::Math::MatrixNd> F;
    for (i = 1; i < this->mBodies.size(); i++)
    {
        F.push_back(F_i);
    }

    // Backward Pass
    std::vector<std::vector<size_t>> subTrees = getDofSubTrees();
    for (i = static_cast<int>(this->mBodies.size() - 1); i > 0; i--)
    {    
        unsigned int q_index_i = static_cast<size_t>(this->mJoints[i].q_index);
        const std::vector<size_t>& sub_tree = subTrees[q_index_i];

        this->U[i] = this->IA[i] * this->S[i];
        this->d[i] = this->S[i].dot(this->U[i]);

        Minv(q_index_i, q_index_i) = 1.0 / (this->d[i]);

        for (j = 0; j < sub_tree.size(); j++) {
              const RigidBodyDynamics::Math::SpatialVector& Ftemp = F[q_index_i].block(0, static_cast<unsigned int>(sub_tree[j]), 6, 1);
              Minv(q_index_i, static_cast<unsigned int>(sub_tree[j])) -= (1.0/this->d[i]) * this->S[i].transpose() * Ftemp;
        }

        size_t lambda = static_cast<size_t>(this->lambda[i]);
        size_t lambda_q_i = static_cast<size_t>(this->mJoints[lambda].q_index);
        if (lambda != 0) {
            for (j = 0; j < sub_tree.size(); j++) {
                F[q_index_i].block(0, static_cast<unsigned int>(sub_tree[j]), 6, 1) += this->U[i] * Minv.block(q_index_i, static_cast<unsigned int>(sub_tree[j]), 1, 1);

                F[lambda_q_i].block(0, static_cast<unsigned int>(sub_tree[j]), 6, 1) += this->X_lambda[i].toMatrixTranspose() * F[q_index_i].block(0, static_cast<unsigned int>(sub_tree[j]), 6, 1);
            }

            RigidBodyDynamics::Math::SpatialMatrix Ia = this->IA[i]
                - this->U[i]
                * (this->U[i] / this->d[i]).transpose();

#ifdef BIORBD_USE_CASADI_MATH
          this->IA[lambda]
            += this->X_lambda[i].toMatrixTranspose()
            * Ia * this->X_lambda[i].toMatrix();

#else
          this->IA[lambda].noalias()
            += this->X_lambda[i].toMatrixTranspose()
            * Ia * this->X_lambda[i].toMatrix();
#endif
        }
    }
    // End Backward Pass

    // Second Forward Pass
    for (i = 1; i < this->mBodies.size(); i++) {
      unsigned int q_index_i = this->mJoints[i].q_index;
      unsigned int lambda = this->lambda[i];
      unsigned int lambda_q_i = this->mJoints[lambda].q_index;

      RigidBodyDynamics::Math::SpatialTransform X_lambda = this->X_lambda[i];

        if (lambda != 0){
            // Minv[i,i:] = Dinv[i]* (U[i,:].transpose() * Xmat) * F[lambda,:,i:])
            for (j = q_index_i; j < static_cast<int>(this->dof_count); j++) {
//                RigidBodyDynamics::Math::SpatialVector Ftemp = F[lambda_q_i].block(0, q_index_i, 6, 1);
                RigidBodyDynamics::Math::SpatialVector Ftemp = F[lambda_q_i].block(0, j, 6, 1);
                Minv(q_index_i, j) -=
                        (1.0/this->d[i]) * (this->U[i].transpose() * X_lambda.toMatrix()) * Ftemp;
            }

        }
        // F[i,:,i:] = np.outer(S,Minv[i,i:]) // could be simplified (S * M[q_index_i,q_index_i:]^T)
        for (j = q_index_i; j < static_cast<int>(this->dof_count); j++) {
                    F[q_index_i].block(0, j, 6, 1) = this->S[i] * Minv.block(q_index_i, j, 1, 1); // outer product
        }


        if (lambda != 0){
            //  F[i,:,i:] += Xmat.transpose() * F[lambda,:,i:]
            for (j = q_index_i; j < static_cast<int>(this->dof_count); j++) {
                F[q_index_i].block(0, j, 6, 1) +=
                        X_lambda.toMatrix() * F[lambda_q_i].block(0, j, 6, 1);
            }

        }
    }
    // End of Second Forward Pass
    // Fill in full matrix (currently only upper triangular)
    for (j = 0; j < static_cast<int>(this->dof_count); j++)
    {
        for (i = 0; i < static_cast<int>(this->dof_count); i++)
        {
            if (j < i) {
                    Minv(i, j) = Minv(j, i);
            }
        }
    }

    return Minv;
}

utils::Vector3d rigidbody::Joints::CoMdot(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    // For each segment, find the CoM
    utils::Vector3d com_dot(0,0,0);

    // CoMdot = sum(mass_seg * Jacobian * qdot)/mass totale
    utils::Matrix Jac(utils::Matrix(3,this->dof_count));
    for (const auto& segment : *m_segments) {
        Jac.setZero();
        RigidBodyDynamics::CalcPointJacobian(
            *this, Q, GetBodyId(segment.name().c_str()),
            segment.characteristics().mCenterOfMass, Jac, updateKin);
        com_dot += ((Jac*Qdot) * segment.characteristics().mMass);
        updateKin = false;
    }
    // Divide by total mass
    com_dot = com_dot/mass();

    // Return the velocity of CoM
    return com_dot;
}
utils::Vector3d rigidbody::Joints::CoMddot(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    const rigidbody::GeneralizedAcceleration &Qddot,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    utils::Scalar mass;
    RigidBodyDynamics::Math::Vector3d com, com_ddot;
    RigidBodyDynamics::Utils::CalcCenterOfMass(
        *this, Q, Qdot, &Qddot, mass, com, nullptr, &com_ddot,
        nullptr, nullptr, updateKin);


    // Return the acceleration of CoM
    return com_ddot;
}

utils::Matrix rigidbody::Joints::CoMJacobian(
    const rigidbody::GeneralizedCoordinates &Q,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif

    // Total jacobian
    utils::Matrix JacTotal(utils::Matrix::Zero(3,this->dof_count));

    // CoMdot = sum(mass_seg * Jacobian * qdot)/mass total
    utils::Matrix Jac(utils::Matrix::Zero(3,this->dof_count));
    for (auto segment : *m_segments) {
        Jac.setZero();
        RigidBodyDynamics::CalcPointJacobian(
            *this, Q, GetBodyId(segment.name().c_str()),
            segment.characteristics().mCenterOfMass, Jac, updateKin);
        JacTotal += segment.characteristics().mMass*Jac;
        updateKin = false;
    }

    // Divide by total mass
    JacTotal /= this->mass();

    // Return the Jacobian of CoM
    return JacTotal;
}


std::vector<rigidbody::NodeSegment>
rigidbody::Joints::CoMbySegment(
    const rigidbody::GeneralizedCoordinates &Q,
    bool updateKin)
{
    std::vector<rigidbody::NodeSegment> out;
    for (size_t i=0; i<m_segments->size(); ++i) {
        out.push_back(CoMbySegment(Q,i,updateKin));
        updateKin = false;
    }
    return out;
}

utils::Matrix rigidbody::Joints::CoMbySegmentInMatrix(
    const rigidbody::GeneralizedCoordinates &Q,
    bool updateKin)
{
    std::vector<rigidbody::NodeSegment> allCoM(CoMbySegment(Q, updateKin));
    utils::Matrix CoMs(3, static_cast<int>(allCoM.size()));
    for (size_t i=0; i<allCoM.size(); ++i) {
        CoMs.block(0, static_cast<unsigned int>(i), 3, 1) = allCoM[i];
    }
    return CoMs;
}


utils::Vector3d rigidbody::Joints::CoMbySegment(
    const rigidbody::GeneralizedCoordinates &Q,
    const size_t idx,
    bool updateKin)
{
    utils::Error::check(idx < m_segments->size(),
                                "Choosen segment doesn't exist");
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    return RigidBodyDynamics::CalcBodyToBaseCoordinates(
               *this, Q, static_cast<unsigned int>((*m_segments)[idx].id()),
               (*m_segments)[idx].characteristics().mCenterOfMass, updateKin);
}


std::vector<utils::Vector3d> rigidbody::Joints::CoMdotBySegment(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    bool updateKin)
{
    std::vector<utils::Vector3d> out;
    for (size_t i=0; i<m_segments->size(); ++i) {
        out.push_back(CoMdotBySegment(Q,Qdot,i,updateKin));
        updateKin = false;
    }
    return out;
}


utils::Vector3d rigidbody::Joints::CoMdotBySegment(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    const size_t idx,
    bool updateKin)
{
    // Position of the center of mass of segment i
    utils::Error::check(idx < m_segments->size(),
                                "Choosen segment doesn't exist");
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    return CalcPointVelocity(
               *this, Q, Qdot, static_cast<unsigned int>((*m_segments)[idx].id()),
               (*m_segments)[idx].characteristics().mCenterOfMass,updateKin);
}


std::vector<utils::Vector3d>
rigidbody::Joints::CoMddotBySegment(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    const rigidbody::GeneralizedAcceleration &Qddot,
    bool updateKin)
{
    std::vector<utils::Vector3d> out;
    for (size_t i=0; i<m_segments->size(); ++i) {
        out.push_back(CoMddotBySegment(Q,Qdot,Qddot,i,updateKin));
        updateKin = false;
    }
    return out;
}


utils::Vector3d rigidbody::Joints::CoMddotBySegment(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    const rigidbody::GeneralizedAcceleration &Qddot,
    const size_t idx,
    bool updateKin)
{
    utils::Error::check(idx < m_segments->size(),
                                "Choosen segment doesn't exist");
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    return RigidBodyDynamics::CalcPointAcceleration(
               *this, Q, Qdot, Qddot, static_cast<unsigned int>((*m_segments)[idx].id()),
               (*m_segments)[idx].characteristics().mCenterOfMass,updateKin);
}

std::vector<std::vector<utils::Vector3d>>
        rigidbody::Joints::meshPoints(
            const rigidbody::GeneralizedCoordinates &Q,
            bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) {
        UpdateKinematicsCustom (&Q);
    }
    std::vector<std::vector<utils::Vector3d>> v;

    // Find the position of the segments
    const std::vector<utils::RotoTrans>& RT(allGlobalJCS());

    // For all the segments
    for (size_t i=0; i<nbSegment(); ++i) {
        v.push_back(meshPoints(RT,i));
    }

    return v;
}
std::vector<utils::Vector3d> rigidbody::Joints::meshPoints(
    const rigidbody::GeneralizedCoordinates &Q,
    size_t i,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) {
        UpdateKinematicsCustom (&Q);
    }

    // Find the position of the segments
    const std::vector<utils::RotoTrans>& RT(allGlobalJCS());

    return meshPoints(RT,i);
}

std::vector<utils::Matrix>
rigidbody::Joints::meshPointsInMatrix(
    const rigidbody::GeneralizedCoordinates &Q,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    if (updateKin) {
        UpdateKinematicsCustom (&Q);
    }
    const std::vector<utils::RotoTrans>& RT(allGlobalJCS());

    std::vector<utils::Matrix> all_points;
    for (size_t i=0; i<m_segments->size(); ++i) {
        utils::Matrix mat(3, mesh(i).nbVertex());
        for (size_t j=0; j<mesh(i).nbVertex(); ++j) {
            utils::Vector3d tp (mesh(i).point(j));
            tp.applyRT(RT[i]);
            mat.block(0, static_cast<unsigned int>(j), 3, 1) = tp;
        }
        all_points.push_back(mat);
    }
    return all_points;
}
std::vector<utils::Vector3d> rigidbody::Joints::meshPoints(
    const std::vector<utils::RotoTrans> &RT,
    size_t i) const
{

    // Gather the position of the meshings
    std::vector<utils::Vector3d> v;
    for (size_t j=0; j<mesh(i).nbVertex(); ++j) {
        utils::Vector3d tp (mesh(i).point(j));
        tp.applyRT(RT[i]);
        v.push_back(tp);
    }

    return v;
}

std::vector<std::vector<rigidbody::MeshFace>>
        rigidbody::Joints::meshFaces() const
{
    // Gather the position of the meshings for all the segments
    std::vector<std::vector<rigidbody::MeshFace>> v_all;
    for (size_t j=0; j<nbSegment(); ++j) {
        v_all.push_back(meshFaces(j));
    }
    return v_all;
}
const std::vector<rigidbody::MeshFace>
&rigidbody::Joints::meshFaces(size_t idx) const
{
    // Find the position of the meshings for a segment i
    return mesh(idx).faces();
}

std::vector<rigidbody::Mesh> rigidbody::Joints::mesh() const
{
    std::vector<rigidbody::Mesh> segmentOut;
    for (size_t i=0; i<nbSegment(); ++i) {
        segmentOut.push_back(mesh(i));
    }
    return segmentOut;
}

const rigidbody::Mesh &rigidbody::Joints::mesh(
    size_t idx) const
{
    return segment(idx).characteristics().mesh();
}

utils::Vector3d rigidbody::Joints::CalcAngularMomentum (
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    bool updateKin)
{
    RigidBodyDynamics::Math::Vector3d com,  angularMomentum;
    utils::Scalar mass;

    // Calculate the angular momentum with the function of the
    // position of the center of mass
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    RigidBodyDynamics::Utils::CalcCenterOfMass(
        *this, Q, Qdot, nullptr, mass, com, nullptr, nullptr,
        &angularMomentum, nullptr, updateKin);
    return angularMomentum;
}

utils::Vector3d rigidbody::Joints::CalcAngularMomentum (
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    const rigidbody::GeneralizedAcceleration &Qddot,
    bool updateKin)
{
    // Definition of the variables
    RigidBodyDynamics::Math::Vector3d com,  angularMomentum;
    utils::Scalar mass;

    // Calculate the angular momentum with the function of the
    // position of the center of mass
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    RigidBodyDynamics::Utils::CalcCenterOfMass(
        *this, Q, Qdot, &Qddot, mass, com, nullptr, nullptr,
        &angularMomentum, nullptr, updateKin);

    return angularMomentum;
}

std::vector<utils::Vector3d>
rigidbody::Joints::CalcSegmentsAngularMomentum (
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif

    utils::Scalar mass;
    RigidBodyDynamics::Math::Vector3d com;
    RigidBodyDynamics::Utils::CalcCenterOfMass (
        *this, Q, Qdot, nullptr, mass, com, nullptr,
        nullptr, nullptr, nullptr, updateKin);
    RigidBodyDynamics::Math::SpatialTransform X_to_COM (
        RigidBodyDynamics::Math::Xtrans(com));

    std::vector<utils::Vector3d> h_segment;
    for (size_t i = 1; i < this->mBodies.size(); i++) {
        this->Ic[i] = this->I[i];
        this->hc[i] = this->Ic[i].toMatrix() * this->v[i];

        RigidBodyDynamics::Math::SpatialVector h = this->X_lambda[i].applyTranspose (
                    this->hc[i]);
        if (this->lambda[i] != 0) {
            size_t j(i);
            do {
                j = this->lambda[j];
                h = this->X_lambda[j].applyTranspose (h);
            } while (this->lambda[j]!=0);
        }
        h = X_to_COM.applyAdjoint (h);
        h_segment.push_back(utils::Vector3d(h[0],h[1],h[2]));
    }


    return h_segment;
}

std::vector<utils::Vector3d>
rigidbody::Joints::CalcSegmentsAngularMomentum (
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    const rigidbody::GeneralizedAcceleration &Qddot,
    bool updateKin)
{
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif

    utils::Scalar mass;
    RigidBodyDynamics::Math::Vector3d com;
    RigidBodyDynamics::Utils::CalcCenterOfMass (*this, Q, Qdot, &Qddot, mass, com,
            nullptr, nullptr, nullptr, nullptr,
            updateKin);
    RigidBodyDynamics::Math::SpatialTransform X_to_COM (
        RigidBodyDynamics::Math::Xtrans(com));

    std::vector<utils::Vector3d> h_segment;
    for (size_t i = 1; i < this->mBodies.size(); i++) {
        this->Ic[i] = this->I[i];
        this->hc[i] = this->Ic[i].toMatrix() * this->v[i];

        RigidBodyDynamics::Math::SpatialVector h = this->X_lambda[i].applyTranspose (
                    this->hc[i]);
        if (this->lambda[i] != 0) {
            size_t j(i);
            do {
                j = this->lambda[j];
                h = this->X_lambda[j].applyTranspose (h);
            } while (this->lambda[j]!=0);
        }
        h = X_to_COM.applyAdjoint (h);
        h_segment.push_back(utils::Vector3d(h[0],h[1],h[2]));
    }


    return h_segment;
}

size_t rigidbody::Joints::nbQuat() const
{
    return *m_nRotAQuat;
}

rigidbody::GeneralizedVelocity rigidbody::Joints::computeQdot(
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedCoordinates &QDot,
    const utils::Scalar &k_stab)
{
    rigidbody::GeneralizedVelocity QDotOut(static_cast<int>(Q.size()));
    // Verify if there are quaternions, if not the derivate is directly QDot
    if (!m_nRotAQuat) {
        QDotOut = QDot;
        return QDotOut;
    }
    unsigned int cmpQuat(0);
    unsigned int cmpDof(0);
    for (size_t i=0; i<nbSegment(); ++i) {
        const rigidbody::Segment& segment_i = segment(i);
        if (segment_i.isRotationAQuaternion()) {
            // Extraire le quaternion
            utils::Quaternion quat_tp(
                Q(Q.size() - static_cast<unsigned int>(*m_nRotAQuat+cmpQuat)),
                Q.block(cmpDof + static_cast<unsigned int>(segment_i.nbDofTrans()), 0, 3, 1),
                k_stab);

            // QDot for translation is actual QDot
            QDotOut.block(cmpDof, 0, static_cast<unsigned int>(segment_i.nbDofTrans()), 1)
                = QDot.block(cmpDof, 0, static_cast<unsigned int>(segment_i.nbDofTrans()), 1);

            // Get the 4d derivative for the quaternion part
            quat_tp.derivate(QDot.block(cmpDof+ static_cast<unsigned int>(segment_i.nbDofTrans()), 0, 3, 1));
            QDotOut.block(cmpDof+ static_cast<unsigned int>(segment_i.nbDofTrans()), 0, 3, 1) = quat_tp.block(1,0,3,1);
            QDotOut(Q.size()- static_cast<unsigned int>(*m_nRotAQuat+cmpQuat)) = quat_tp(
                        0);// Placer dans le vecteur de sortie

            // Increment the number of done quaternions
            ++cmpQuat;
        } else {
            // If it's a normal, do what it usually does
            QDotOut.block(cmpDof, 0, static_cast<unsigned int>(segment_i.nbDof()), 1) =
                QDot.block(cmpDof, 0, static_cast<unsigned int>(segment_i.nbDof()), 1);
        }
        cmpDof += static_cast<unsigned int>(segment_i.nbDof());
    }
    return QDotOut;
}

utils::Scalar rigidbody::Joints::KineticEnergy(
        const rigidbody::GeneralizedCoordinates &Q,
        const rigidbody::GeneralizedVelocity &QDot,
        bool updateKin)
{
    return RigidBodyDynamics::Utils::CalcKineticEnergy(*this, Q, QDot, updateKin);
}


utils::Scalar rigidbody::Joints::PotentialEnergy(
        const rigidbody::GeneralizedCoordinates &Q,
        bool updateKin)
{
    return RigidBodyDynamics::Utils::CalcPotentialEnergy(*this, Q, updateKin);
}

utils::Scalar rigidbody::Joints::Lagrangian(
        const rigidbody::GeneralizedCoordinates &Q,
        const rigidbody::GeneralizedVelocity &QDot,
        bool updateKin)
{
    return RigidBodyDynamics::Utils::CalcKineticEnergy(*this, Q, QDot, updateKin) - RigidBodyDynamics::Utils::CalcPotentialEnergy(*this, Q, updateKin);
}


utils::Scalar rigidbody::Joints::TotalEnergy(
        const rigidbody::GeneralizedCoordinates &Q,
        const rigidbody::GeneralizedVelocity &QDot,
        bool updateKin)
{
    return RigidBodyDynamics::Utils::CalcKineticEnergy(*this, Q, QDot, updateKin) + RigidBodyDynamics::Utils::CalcPotentialEnergy(*this, Q, updateKin);;
}

rigidbody::GeneralizedTorque rigidbody::Joints::InverseDynamics(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedAcceleration& QDDot
)
{
    rigidbody::ExternalForceSet forceSet(static_cast<BIORBD_NAMESPACE::Model&>(*this));
    return InverseDynamics(Q, QDot, QDDot, forceSet);
}
rigidbody::GeneralizedTorque rigidbody::Joints::InverseDynamics(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedAcceleration& QDDot,
    rigidbody::ExternalForceSet& externalForces
)
{
    rigidbody::GeneralizedTorque Tau(nbGeneralizedTorque());
    auto fExt = externalForces.computeRbdlSpatialVectors(Q, QDot);
    RigidBodyDynamics::InverseDynamics(*this, Q, QDot, QDDot, Tau, &fExt);
    return Tau;
}

rigidbody::GeneralizedTorque rigidbody::Joints::NonLinearEffect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot
)
{
    rigidbody::ExternalForceSet forceSet(static_cast<BIORBD_NAMESPACE::Model&>(*this));
    return NonLinearEffect(Q, QDot, forceSet);
}
rigidbody::GeneralizedTorque rigidbody::Joints::NonLinearEffect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    rigidbody::ExternalForceSet& externalForces
)
{
    rigidbody::GeneralizedTorque Tau(*this);
    auto fExt = externalForces.computeRbdlSpatialVectors(Q, QDot);
    RigidBodyDynamics::NonlinearEffects(*this, Q, QDot, Tau, &fExt);
    return Tau;
}

rigidbody::GeneralizedAcceleration rigidbody::Joints::ForwardDynamics(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedTorque& Tau
)
{
    rigidbody::ExternalForceSet forceSet(static_cast<BIORBD_NAMESPACE::Model&>(*this));
    return ForwardDynamics(Q, QDot, Tau, forceSet);
}
rigidbody::GeneralizedAcceleration rigidbody::Joints::ForwardDynamics(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedTorque& Tau,
    rigidbody::ExternalForceSet& externalForces
)
{
    rigidbody::GeneralizedAcceleration QDDot(*this);
    auto fExt = externalForces.computeRbdlSpatialVectors(Q, QDot, true);
    RigidBodyDynamics::ForwardDynamics(*this, Q, QDot, Tau, QDDot, &fExt);
    return QDDot;
}

rigidbody::GeneralizedAcceleration rigidbody::Joints::ForwardDynamicsFreeFloatingBase(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedAcceleration& QJointsDDot)
{

    utils::Error::check(QJointsDDot.size() == this->nbQddot() - this->nbRoot(),
                        "Size of QDDotJ must be equal to number of QDDot - number of root coordinates.");
    
    utils::Error::check(this->nbRoot() > 0, "Must have a least one degree of freedom on root.");

    rigidbody::GeneralizedAcceleration QDDot(this->nbQddot());
    rigidbody::GeneralizedAcceleration QRootDDot;
    rigidbody::GeneralizedTorque MassMatrixNlEffects;

    utils::Matrix massMatrixRoot = this->massMatrix(Q).block(0, 0, static_cast<unsigned int>(this->nbRoot()), static_cast<unsigned int>(this->nbRoot()));

    QDDot.block(0, 0, static_cast<unsigned int>(this->nbRoot()), 1) = utils::Vector(this->nbRoot()).setZero();
    QDDot.block(static_cast<unsigned int>(this->nbRoot()), 0, static_cast<unsigned int>(this->nbQddot()-this->nbRoot()), 1) = QJointsDDot;

    MassMatrixNlEffects = InverseDynamics(Q, QDot, QDDot);

#ifdef BIORBD_USE_CASADI_MATH
    auto linsol = casadi::Linsol("linsol", "symbolicqr", massMatrixRoot.sparsity());
    QRootDDot = linsol.solve(massMatrixRoot, -MassMatrixNlEffects.block(0, 0, static_cast<unsigned int>(this->nbRoot()), 1));
#else
    QRootDDot = massMatrixRoot.llt().solve(-MassMatrixNlEffects.block(0, 0, static_cast<unsigned int>(this->nbRoot()), 1));
#endif

    return QRootDDot;
}


rigidbody::GeneralizedAcceleration rigidbody::Joints::ForwardDynamicsConstraintsDirect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedTorque& Tau
)
{
    rigidbody::Contacts CS = dynamic_cast<rigidbody::Contacts*>(this)->getConstraints();
    return ForwardDynamicsConstraintsDirect(Q, QDot, Tau, CS);
}
rigidbody::GeneralizedAcceleration rigidbody::Joints::ForwardDynamicsConstraintsDirect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedTorque& Tau,
    rigidbody::ExternalForceSet& externalForces
)
{
    rigidbody::Contacts CS = dynamic_cast<rigidbody::Contacts*>(this)->getConstraints();
    return this->ForwardDynamicsConstraintsDirect(Q, QDot, Tau, CS, externalForces);
}
rigidbody::GeneralizedAcceleration rigidbody::Joints::ForwardDynamicsConstraintsDirect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedTorque& Tau,
    rigidbody::Contacts& CS
)
{
    rigidbody::ExternalForceSet forceSet(static_cast<BIORBD_NAMESPACE::Model&>(*this));
    return ForwardDynamicsConstraintsDirect(Q, QDot, Tau, CS, forceSet);
}
rigidbody::GeneralizedAcceleration rigidbody::Joints::ForwardDynamicsConstraintsDirect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedTorque& Tau,
    rigidbody::Contacts& CS,
    rigidbody::ExternalForceSet& externalForces
)
{
#ifdef BIORBD_USE_CASADI_MATH
    bool updateKin = true;
#else
    UpdateKinematicsCustom(&Q, &QDot);
    bool updateKin = false;  // Put this in parameters??
#endif

    rigidbody::GeneralizedAcceleration QDDot(*this);
    auto fExt = externalForces.computeRbdlSpatialVectors(Q, QDot, true);
    RigidBodyDynamics::ForwardDynamicsConstraintsDirect(*this, Q, QDot, Tau, CS, QDDot, updateKin, &fExt);
    return QDDot;
}

utils::Vector rigidbody::Joints::ContactForcesFromForwardDynamicsConstraintsDirect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedTorque& Tau
)
{
    rigidbody::ExternalForceSet forceSet(static_cast<BIORBD_NAMESPACE::Model&>(*this));
    return ContactForcesFromForwardDynamicsConstraintsDirect(Q, QDot, Tau, forceSet);
}
utils::Vector rigidbody::Joints::ContactForcesFromForwardDynamicsConstraintsDirect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDot,
    const rigidbody::GeneralizedTorque& Tau,
    rigidbody::ExternalForceSet& externalForces
)
{
    rigidbody::Contacts CS = dynamic_cast<rigidbody::Contacts*> (this)->getConstraints();
    this->ForwardDynamicsConstraintsDirect(Q, QDot, Tau, CS, externalForces);
    return CS.getForce();
}

rigidbody::GeneralizedVelocity rigidbody::Joints::ComputeConstraintImpulsesDirect(
    const rigidbody::GeneralizedCoordinates& Q,
    const rigidbody::GeneralizedVelocity& QDotPre
)
{
    rigidbody::Contacts CS = dynamic_cast<rigidbody::Contacts*>(this)->getConstraints();
    if (CS.nbContacts() == 0) {
        return QDotPre;
    } else {
        CS = dynamic_cast<rigidbody::Contacts*>(this)->getConstraints();

        rigidbody::GeneralizedVelocity QDotPost(*this);
        RigidBodyDynamics::ComputeConstraintImpulsesDirect(*this, Q, QDotPre, CS, QDotPost);
        return QDotPost;
    }
}

utils::Matrix3d rigidbody::Joints::bodyInertia (
        const rigidbody::GeneralizedCoordinates &q,
        bool updateKin)
{
  if (updateKin) {
    this->UpdateKinematicsCustom (&q);
  }

  for (size_t i = 1; i < this->mBodies.size(); i++) {
    this->Ic[i] = this->I[i];
  }

  RigidBodyDynamics::Math::SpatialRigidBodyInertia Itot;

  for (size_t i = this->mBodies.size() - 1; i > 0; i--) {
    size_t lambda = static_cast<size_t>(this->lambda[i]);

    if (lambda != 0) {
      this->Ic[lambda] = this->Ic[lambda] + this->X_lambda[i].applyTranspose (
                           this->Ic[i]);
    } else {
      Itot = Itot + this->X_lambda[i].applyTranspose (this->Ic[i]);
    }
  }

  utils::Vector3d com = Itot.h / Itot.m;
  return RigidBodyDynamics::Math::Xtrans(-com).applyTranspose(Itot).toMatrix().block(0, 0, 3, 3);
}

utils::Vector3d rigidbody::Joints::bodyAngularVelocity (
    const rigidbody::GeneralizedCoordinates &Q,
    const rigidbody::GeneralizedVelocity &Qdot,
    bool updateKin)
{
    RigidBodyDynamics::Math::Vector3d com, angularMomentum;
    utils::Scalar mass;
    // utils::Matrix3d body_inertia;

    // Calculate the angular velocity of the model around it's center of
    // mass from the angular momentum and the body inertia
#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    RigidBodyDynamics::Utils::CalcCenterOfMass(
        *this, Q, Qdot, nullptr, mass, com, nullptr, nullptr,
        &angularMomentum, nullptr, updateKin);
    utils::Matrix3d body_inertia = bodyInertia (Q, updateKin);
        
#ifdef BIORBD_USE_CASADI_MATH
    auto linsol = casadi::Linsol("linear_solver", "symbolicqr", body_inertia.sparsity());
    RigidBodyDynamics::Math::Vector3d out = linsol.solve(body_inertia, angularMomentum);
#else
    RigidBodyDynamics::Math::Vector3d out = body_inertia.colPivHouseholderQr().solve(angularMomentum);
#endif

    return out;
}

size_t rigidbody::Joints::getDofIndex(
    const utils::String& segmentName,
    const utils::String& dofName)
{
    size_t idx = 0;

    size_t iB = 0;
    bool found = false;
    while (1) {
        utils::Error::check(iB!=m_segments->size(), "Segment not found");

        if (segmentName.compare(  (*m_segments)[iB].name() )   ) {
            idx +=  (*m_segments)[iB].nbDof();
        } else {
            idx += (*m_segments)[iB].getDofIdx(dofName);
            found = true;
            break;
        }

        ++iB;
    }

    utils::Error::check(found, "Dof not found");
    return idx;
}

void rigidbody::Joints::UpdateKinematicsCustom(
    const rigidbody::GeneralizedCoordinates *Q,
    const rigidbody::GeneralizedVelocity *Qdot,
    const rigidbody::GeneralizedAcceleration *Qddot)
{
    checkGeneralizedDimensions(Q, Qdot, Qddot);
    RigidBodyDynamics::UpdateKinematicsCustom(*this, Q, Qdot, Qddot);
}

void rigidbody::Joints::CalcMatRotJacobian(
    const rigidbody::GeneralizedCoordinates &Q,
    size_t segmentIdx,
    const utils::Matrix3d &rotation,
    RigidBodyDynamics::Math::MatrixNd &G,
    bool updateKin)
{
#ifdef RBDL_ENABLE_LOGGING
    LOG << "-------- " << __func__ << " --------" << std::endl;
#endif

#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif
    // update the Kinematics if necessary
    if (updateKin) {
        UpdateKinematicsCustom (&Q, nullptr, nullptr);
    }

    assert (G.rows() == 9 && G.cols() == this->qdot_size );

    std::vector<utils::Vector3d> axes;
    axes.push_back(utils::Vector3d(1,0,0));
    axes.push_back(utils::Vector3d(0,1,0));
    axes.push_back(utils::Vector3d(0,0,1));
    for (unsigned int iAxes=0; iAxes<3; ++iAxes) {
        utils::Matrix3d bodyMatRot (
            RigidBodyDynamics::CalcBodyWorldOrientation (*this, Q, static_cast<unsigned int>(segmentIdx), false).transpose());
        RigidBodyDynamics::Math::SpatialTransform point_trans(
            RigidBodyDynamics::Math::SpatialTransform (
                utils::Matrix3d::Identity(),
                bodyMatRot * rotation * *(axes.begin()+iAxes)
            )
        );

        size_t reference_body_id = segmentIdx;
        if (this->IsFixedBodyId(static_cast<unsigned int>(segmentIdx))) {
            size_t fbody_id = segmentIdx - this->fixed_body_discriminator;
            reference_body_id = this->mFixedBodies[fbody_id].mMovableParent;
        }
        size_t j = reference_body_id;

        // e[j] is set to 1 if joint j contributes to the jacobian that we are
        // computing. For all other joints the column will be zero.
        while (j != 0) {
            unsigned int q_index = this->mJoints[j].q_index;
            // If it's not a DoF in translation (3 4 5 in this->S)
#ifdef BIORBD_USE_CASADI_MATH
            if (this->S[j].is_zero() && this->S[j](4).is_zero() && this->S[j](5).is_zero())
#else
            if (this->S[j](3)!=1.0 && this->S[j](4)!=1.0 && this->S[j](5)!=1.0)
#endif
            {
                RigidBodyDynamics::Math::SpatialTransform X_base = this->X_base[j];
                X_base.r = utils::Vector3d(0,0,0); // Remove all concept of translation (only keep the rotation matrix)

                if (this->mJoints[j].mDoFCount == 3) {
                    G.block(iAxes*3, q_index, 3, 3) 
                        = ((point_trans * X_base.inverse()).toMatrix() * this->multdof3_S[j]).block(3,0,3,3);
                } else {
                    G.block(iAxes*3,q_index, 3, 1) 
                        = point_trans.apply(X_base.inverse().apply(this->S[j])).block(3,0,3,1);
                }
            }
            j = this->lambda[j]; // Pass to parent segment
        }
    }
}

utils::Matrix
rigidbody::Joints::JacobianSegmentRotMat(
        const rigidbody::GeneralizedCoordinates &Q,
        size_t biorbdSegmentIdx,
        bool updateKin)
{

#ifdef BIORBD_USE_CASADI_MATH
    updateKin = true;
#endif

    size_t segmentIdx = getBodyBiorbdIdToRbdlId(static_cast<int>(biorbdSegmentIdx));

    utils::Matrix jacobianMat(utils::Matrix::Zero(9, static_cast<unsigned int>(nbQ())));
    CalcMatRotJacobian(
        Q,
        segmentIdx,
        utils::Matrix3d::Identity(),
        jacobianMat,
        updateKin);

    return jacobianMat;
}


void rigidbody::Joints::checkGeneralizedDimensions(
    const rigidbody::GeneralizedCoordinates *Q,
    const rigidbody::GeneralizedVelocity *Qdot,
    const rigidbody::GeneralizedAcceleration *Qddot,
    const rigidbody::GeneralizedTorque *torque)
{
#ifndef SKIP_ASSERT
    if (Q) {
        utils::Error::check(
            Q->size() == nbQ(),
            "Wrong size for the Generalized Coordiates, " + 
            utils::String("expected ") + std::to_string(nbQ()) + " got " + std::to_string(Q->size()));
    }
    if (Qdot) {
        utils::Error::check(
            Qdot->size() == nbQdot(),
            "Wrong size for the Generalized Velocities, " +
            utils::String("expected ") + std::to_string(nbQdot()) + " got " + std::to_string(Qdot->size()));
    }
    if (Qddot) {
        utils::Error::check(
            Qddot->size() == nbQddot(),
            "Wrong size for the Generalized Accelerations, " +
            utils::String("expected ") + std::to_string(nbQddot()) + " got " + std::to_string(Qddot->size()));
    }

    if (torque) {
        utils::Error::check(
            torque->size() == nbGeneralizedTorque(),
            "Wrong size for the Generalized Torques, " +
            utils::String("expected ") + std::to_string(nbGeneralizedTorque()) + " got " + std::to_string(torque->size()));
    }
#endif
}
