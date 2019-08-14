#define BIORBD_API_EXPORTS
#include "Muscles/StaticOptimization.h"

#include <IpIpoptApplication.hpp>
#include "BiorbdModel.h"
#include "Utils/Error.h"
#include "Muscles/StateDynamics.h"
#include "Muscles/StaticOptimizationIpoptLinearized.h"

biorbd::muscles::StaticOptimization::StaticOptimization(
        biorbd::Model& model,
        const biorbd::utils::GenCoord &Q,
        const biorbd::utils::GenCoord &Qdot,
        const biorbd::utils::Tau &tauTarget,
        const biorbd::utils::Vector &initialActivationGuess,
        unsigned int pNormFactor,
        bool useResidualTorque,
        int verbose) :
    m_model(model),
    m_useResidualTorque(useResidualTorque),
    m_pNormFactor(pNormFactor),
    m_verbose(verbose),
    m_alreadyRun(false)
{
    m_allQ.push_back(Q);
    m_allQdot.push_back(Qdot);
    m_allTauTarget.push_back(tauTarget);

    if (initialActivationGuess.size() == 0){
        m_initialActivationGuess = biorbd::utils::Vector(m_model.nbMuscleTotal());
        for (unsigned int i=0; i<m_model.nbMuscleTotal(); ++i)
            m_initialActivationGuess[i] = 0.01;
    }
    else
        m_initialActivationGuess = initialActivationGuess;
}

biorbd::muscles::StaticOptimization::StaticOptimization(
        biorbd::Model& model,
        const biorbd::utils::GenCoord &Q,
        const biorbd::utils::GenCoord &Qdot,
        const biorbd::utils::Tau &tauTarget,
        const std::vector<biorbd::muscles::StateDynamics> &initialActivationGuess,
        unsigned int pNormFactor,
        bool useResidualTorque,
        int verbose
        ) :
    m_model(model),
    m_useResidualTorque(useResidualTorque),
    m_pNormFactor(pNormFactor),
    m_verbose(verbose),
    m_alreadyRun(false)
{
    m_allQ.push_back(Q);
    m_allQdot.push_back(Qdot);
    m_allTauTarget.push_back(tauTarget);

    m_initialActivationGuess = biorbd::utils::Vector(m_model.nbMuscleTotal());
    for (unsigned int i = 0; i<m_model.nbMuscleTotal(); i++)
        m_initialActivationGuess[i] = initialActivationGuess[i].activation();
}

biorbd::muscles::StaticOptimization::StaticOptimization(
        biorbd::Model &model,
        const std::vector<biorbd::utils::GenCoord> &allQ,
        const std::vector<biorbd::utils::GenCoord> &allQdot,
        const std::vector<biorbd::utils::Tau> &allTauTarget,
        const biorbd::utils::Vector &initialActivationGuess,
        unsigned int pNormFactor,
        bool useResidualTorque,
        int verbose) :
    m_model(model),
    m_useResidualTorque(useResidualTorque),
    m_allQ(allQ),
    m_allQdot(allQdot),
    m_allTauTarget(allTauTarget),
    m_pNormFactor(pNormFactor),
    m_verbose(verbose),
    m_alreadyRun(false)
{
    if (initialActivationGuess.size() == 0){
        m_initialActivationGuess = biorbd::utils::Vector(m_model.nbMuscleTotal());
        for (unsigned int i=0; i<m_model.nbMuscleTotal(); ++i)
            m_initialActivationGuess[i] = 0.01;
    }
    else
        m_initialActivationGuess = initialActivationGuess;
}

biorbd::muscles::StaticOptimization::StaticOptimization(
        biorbd::Model& model,
        const std::vector<biorbd::utils::GenCoord> &allQ,
        const std::vector<biorbd::utils::GenCoord> &allQdot,
        const std::vector<biorbd::utils::Tau> &allTauTarget,
        const std::vector<biorbd::muscles::StateDynamics> &initialActivationGuess,
        unsigned int pNormFactor,
        bool useResidualTorque,
        int verbose):
    m_model(model),
    m_useResidualTorque(useResidualTorque),
    m_allQ(allQ),
    m_allQdot(allQdot),
    m_allTauTarget(allTauTarget),
    m_pNormFactor(pNormFactor),
    m_verbose(verbose),
    m_alreadyRun(false)
{
    m_initialActivationGuess = biorbd::utils::Vector(m_model.nbMuscleTotal());
    for (unsigned int i = 0; i<m_model.nbMuscleTotal(); i++)
        m_initialActivationGuess[i] = initialActivationGuess[i].activation();
}

void biorbd::muscles::StaticOptimization::run(bool LinearizedState)
{
    // Setup the Ipopt problem
    Ipopt::SmartPtr<Ipopt::IpoptApplication> app = IpoptApplicationFactory();
    app->Options()->SetNumericValue("tol", 1e-7);
    app->Options()->SetStringValue("mu_strategy", "adaptive");
    //app->Options()->SetStringValue("output_file", "ipopt.out");
    app->Options()->SetStringValue("hessian_approximation", "limited-memory");
    app->Options()->SetStringValue("derivative_test", "first-order");
    app->Options()->SetIntegerValue("max_iter", 10000);

    Ipopt::ApplicationReturnStatus status;
    status = app->Initialize();
    biorbd::utils::Error::error(status == Ipopt::Solve_Succeeded, "Ipopt initialization failed");

    for (unsigned int i=0; i<m_allQ.size(); ++i){
        if (LinearizedState)
            m_staticOptimProblem.push_back(
                        new biorbd::muscles::StaticOptimizationIpoptLinearized(
                            m_model, m_allQ[i], m_allQdot[i], m_allTauTarget[i], m_initialActivationGuess,
                            m_useResidualTorque, m_pNormFactor, m_verbose
                            )
                        );
        else
            m_staticOptimProblem.push_back(
                        new biorbd::muscles::StaticOptimizationIpopt(
                            m_model, m_allQ[i], m_allQdot[i], m_allTauTarget[i], m_initialActivationGuess,
                            m_useResidualTorque, m_pNormFactor, m_verbose
                            )
                        );
        // Optimize!
        status = app->OptimizeTNLP(m_staticOptimProblem[i]);

        // Take the solution of the previous optimization as the solution for the next optimization
        m_initialActivationGuess = static_cast<biorbd::muscles::StaticOptimizationIpopt*>(Ipopt::GetRawPtr(m_staticOptimProblem[i]))->finalSolution();
    }
    m_alreadyRun = true;
}

std::vector<biorbd::utils::Vector> biorbd::muscles::StaticOptimization::finalSolution()
{
    std::vector<biorbd::utils::Vector> res;
    if (!m_alreadyRun){
        biorbd::utils::Error::error(0, "Problem has not been ran through the optimization process yet, you should optimize it first to get the optimized solution");
    }
    else {
        for (unsigned int i=0; i<m_allQ.size(); ++i){
            res.push_back(static_cast<biorbd::muscles::StaticOptimizationIpopt*>(Ipopt::GetRawPtr(m_staticOptimProblem[i]))->finalSolution());
        }
    }

    return res;
}

biorbd::utils::Vector biorbd::muscles::StaticOptimization::finalSolution(unsigned int index)
{
    biorbd::utils::Vector res;
    if (!m_alreadyRun){
        biorbd::utils::Error::error(0, "Problem has not been ran through the optimization process yet, you should optimize it first to get the optimized solution");
    }
    else {
        res = static_cast<biorbd::muscles::StaticOptimizationIpopt*>(Ipopt::GetRawPtr(m_staticOptimProblem[index]))->finalSolution();
        }
    return res;
}

