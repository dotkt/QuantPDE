////////////////////////////////////////////////////////////////////////////////
// gmwb.cpp
// --------
//
// Computes the price of a Guaranteed Minimum Withdrawal Benefit (GMWB) using an
// implicit, impulse control formulation.
//
// Author: Parsiad Azimzadeh
////////////////////////////////////////////////////////////////////////////////

#include <QuantPDE/Core>
#include <QuantPDE/Modules/Lambdas>
#include <QuantPDE/Modules/Operators>

#include "gmwb.hpp"

////////////////////////////////////////////////////////////////////////////////

#include <algorithm> // max, min, max_element
#include <cmath>     // sqrt
#include <iomanip>   // setw
#include <iostream>  // cout
#include <memory>    // unique_ptr
#include <numeric>   // accumulate

////////////////////////////////////////////////////////////////////////////////

using namespace QuantPDE;
using namespace QuantPDE::Modules;

using namespace std;

////////////////////////////////////////////////////////////////////////////////

class ContinuousWithdrawal final : public ControlledLinearSystem2,
		public IterationNode {

	RectilinearGrid2 &grid;
	Noncontrollable2 contractRate;

	Controllable2 control;

public:

	template <typename G, typename F1>
	ContinuousWithdrawal(G &grid, F1 &&contractRate) noexcept
			: grid(grid), contractRate(contractRate),
			control( Control2(grid) ) {
		registerControl(control);
	}

	virtual Matrix A(Real t) {
		Matrix M = grid.matrix();
		M.reserve(IntegerVector::Constant(grid.size(), 3));

		auto M_G = grid.indexer(M);

		const Axis &S = grid[0];
		const Axis &W = grid[1];

		// W > 0
		for(Index j = 1; j < W.size(); ++j) {
			// S = 0
			{
				const Real G = contractRate(t, S[0], W[j]);
				const Real gamma = control(t, S[0], W[j]) * G;

				const Real tW = gamma / (W[j] - W[j-1]);

				M_G(0, j, 0, j    ) =   tW;
				M_G(0, j, 0, j - 1) = - tW;
			}

			// S > 0
			for(Index i = 1; i < S.size(); ++i) {
				const Real G = contractRate(t, S[i], W[j]);
				const Real gamma = control(t, S[i], W[j]) * G;

				const Real tW = gamma / (W[j] - W[j-1]);
				const Real tS = gamma / (S[i] - S[i-1]);

				M_G(i, j, i    , j    ) =   tW + tS;
				M_G(i, j, i    , j - 1) = - tW;
				M_G(i, j, i - 1, j    ) =      - tS;
			}
		}

		M.makeCompressed();
		return M;
	}

	virtual Vector b(Real t) {
		Vector b = grid.vector();

		for(auto node : accessor(grid, b)) {
			const Real S = (&node)[0]; // Investment
			const Real W = (&node)[1]; // Withdrawal

			const Real G = contractRate(t, S, W);
			const Real gamma = control(t, S, W) * G;

			*node = gamma;
		}

		return b;
	}

};

////////////////////////////////////////////////////////////////////////////////

int main() {

	// 2014-10-15: Tested without withdrawals; closed-form is
	// dm :=   (log(S0 / (1-kappa) * (r - alpha - 1/2 * sigma * sigma) * T))
	//       / (sigma * sqrt(T))
	// V   =   S0 * exp(-alpha T) * normcdf(dm + sigma * sqrt(T))
	//       + W0   exp(-r   * T) * (1 - kappa) * (1 - normcdf(dm))

	int n = 10; // Initial optimal control partition size
	int N = 32; // Initial number of timesteps

	Real T = 10.; // 14.28;
	Real r = .05;
	Real v = .2;

	Real w_0 = 100.;

	Real alpha = 0.01389; // 0.036; // Hedging fee

	Real G = 10.; // 7.; // Contract rate
	Real kappa = 0.1; // Penalty rate

	int refinement = 5;

	////////////////////////////////////////////////////////////////////////
	// Solution grid
	////////////////////////////////////////////////////////////////////////

	RectilinearGrid2 grid(
		Axis {
			0., 5., 10., 15., 20., 25.,
			30., 35., 40., 45.,
			50., 55., 60., 65., 70., 72.5, 75., 77.5, 80., 82., 84.,
			86., 88., 90., 91., 92., 93., 94., 95.,
			96., 97., 98., 99., 100.,
			101., 102., 103., 104., 105., 106.,
			107., 108., 109., 110., 112., 114.,
			116., 118., 120., 123., 126.,
			130., 135., 140., 145., 150., 160., 175., 200., 225.,
			250., 300., 500., 750., 1000.
		},
		Axis::range(0., 2., 100.)
	);

	////////////////////////////////////////////////////////////////////////
	// Table headers
	////////////////////////////////////////////////////////////////////////

	cout.precision(6);
	Real previousValue = nan(""), previousChange = nan("");

	const int td = 20;
	cout
		<< setw(td) << "Nodes"                           << "\t"
		<< setw(td) << "Control Nodes"                   << "\t"
		<< setw(td) << "Time Steps"                      << "\t"
		<< setw(td) << "Value"                           << "\t"
		<< setw(td) << "Mean Inner Iterations"           << "\t"
		<< setw(td) << "Std Inner Iterations"            << "\t"
		<< setw(td) << "Max Inner Iterations"            << "\t"
		<< setw(td) << "Change"                          << "\t"
		<< setw(td) << "Ratio"
		<< endl
	;

	for(
		int l = 0, outer = N, partitionSize = n;
		l < refinement;
		++l, outer *= 2, partitionSize *= 2
	) {

		////////////////////////////////////////////////////////////////
		// Control grid
		////////////////////////////////////////////////////////////////

		// Control partition 0 : 1/n : 1 (MATLAB notation)
		#if   defined(GMWB_SURRENDER)
			RectilinearGrid1 impulseControls( Axis { 1. } );
			RectilinearGrid1 continuousControls( Axis { 0. } );
		#elif defined(GMWB_CONTRACT_WITHDRAWAL)
			RectilinearGrid1 impulseControls( Axis { 0. } );
			RectilinearGrid1 continuousControls( Axis { 1. } );
		#else
			// No need to check control = 0
			RectilinearGrid1 impulseControls( Axis::range(
				1. / partitionSize,
				1. / partitionSize,
				1.
			) );
			RectilinearGrid1 continuousControls( Axis { 0., 1. } );
		#endif

		////////////////////////////////////////////////////////////////
		// Iteration tree
		////////////////////////////////////////////////////////////////

		ReverseConstantStepper stepper(
			0.,       // Initial time
			T,        // Expiry time
			T / outer // Timestep size
		);
		ToleranceIteration tolerance;
		stepper.setInnerIteration(tolerance);

		////////////////////////////////////////////////////////////////
		// Linear system tree
		////////////////////////////////////////////////////////////////

		// Black-scholes
		unique_ptr<LinearSystem> blackScholes(
			new BlackScholes<2, 0>(
				grid,
				r, v, alpha
			)
		);

		// Continuous withdrawal
		ContinuousWithdrawal continuousWithdrawal(grid, G);
		continuousWithdrawal.setIteration(stepper);

		// Policy iteration
		unique_ptr<LinearSystem> continuousPolicy;
		{
			MinPolicyIteration2_1 *tmp = new MinPolicyIteration2_1(
				grid,
				continuousControls,
				continuousWithdrawal
			);
			tmp->setIteration(tolerance);
			continuousPolicy = unique_ptr<LinearSystem>(tmp);
		}

		// Linear system sum
		auto sum = move(blackScholes) + move(continuousPolicy);

		// Discretization
		ReverseLinearBDFOne discretization(grid, sum);
		discretization.setIteration(stepper);

		// Impulse withdrawal
		ImpulseWithdrawal impulseWithdrawal(grid, kappa);

		// Impulse withdrawal policy iteration
		MinPolicyIteration2_1 impulsePolicy(
			grid,
			impulseControls,
			impulseWithdrawal
		);
		impulsePolicy.setIteration(tolerance);

		// Penalty method
		PenaltyMethod penalty(grid, discretization, impulsePolicy);
		penalty.setIteration(tolerance);

		////////////////////////////////////////////////////////////////
		// Payoff
		////////////////////////////////////////////////////////////////

		Function2 payoff = [=] (Real S, Real W) {
			return max(S, (1 - kappa) * W);
		};

		////////////////////////////////////////////////////////////////
		// Running
		////////////////////////////////////////////////////////////////

		BiCGSTABSolver solver;

		auto V = stepper.solve(
			grid,    // Domain
			payoff,  // Initial condition
			penalty, // Root of linear system tree
			solver   // Linear system solver
		);

		////////////////////////////////////////////////////////////////
		// Print table rows
		////////////////////////////////////////////////////////////////

		RectilinearGrid2 printGrid(
			Axis::range(0., 25., 200.),
			Axis { 100. }
		);
		cout << accessor( printGrid, V ) << endl;

		auto its = tolerance.iterations();

		Real
			value = V(w_0, w_0),
			var = 0.,
			mean = accumulate(its.begin(),its.end(),0.)/its.size(),
			change = value - previousValue,
			ratio = previousChange / change
		;
		for(auto x : its) { var += (x - mean) * (x - mean); }
		int max = ( *max_element(its.begin(), its.end()) );

		cout
			<< setw(td) << grid.size()            << "\t"
			<< setw(td) << impulseControls.size() << "\t"
			<< setw(td) << outer                  << "\t"
			<< setw(td) << value                  << "\t"
			<< setw(td) << mean                   << "\t"
			<< setw(td) << sqrt(var)              << "\t"
			<< setw(td) << max                    << "\t"
			<< setw(td) << change                 << "\t"
			<< setw(td) << ratio
			<< endl
		;

		previousChange = change;
		previousValue = value;

		////////////////////////////////////////////////////////////////
		// Refine Solution grid
		////////////////////////////////////////////////////////////////

		grid.refine( RectilinearGrid2::NewTickBetweenEachPair() );
	}

	return 0;
}