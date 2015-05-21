##########################
## PROBLEM SPECIFICATION #
##########################

## Dimensions $d$
spatial_dimension = 1;
stochastic_control_dimension = 1;
impulse_control_dimension = 1;

## Expiry $T$
expiry = inf;

## Volatility $\sigma$
function res = volatility (t, x)
  res = [0.3];
endfunction

## Drift $\mu$
function [controlled, uncontrolled] = drift (t, x, q)
  a = 0.25;
  controlled = [-a * q];
  uncontrolled = [0.];
endfunction

## Discount factor $\rho$
function res = discount (t, x, q)
  res = 0.02;
endfunction

## Continuous flow $f$
function [controlled, uncontrolled] = continuous_flow (t, x, q)
  b = 3.;
  m = 0.;
  tmp = max(x - m, 0.);
  controlled = [-q * q * b];
  uncontrolled = [-tmp * tmp];
endfunction

## Intervention transition function $\Gamma$
function res = transition (t, x, x_new)
  res = [x_new];
endfunction

## Intervention flow $K$
function res = impulse_flow (t, x, x_new)
  c = 0.;
  lambda = 1.;
  zeta = x_new - x;
  res = -lambda * abs(zeta) - c;
endfunction

## Flow $g$ upon exiting the parabolic (resp. elliptic for infinite-horizon
## problems) boundary $
function res = exit_flow (t, x)
  res = 0.;
endfunction

####################
## DISCRETIZATION ##
####################

## Number of timesteps to use
timesteps = 32;

## Spatial grid
spatial_grid = { [-6 -4.96909 -4.11317 -3.40211 -2.81087 -2.31861 -1.90802 ...
		-1.56461 -1.27631 -1.03296 -0.825989 -0.648105 -0.493045   ...
		-0.355349 -0.230168 -0.113093 0 0.113093 0.230168 0.355349 ...
		0.493045 0.648105 0.825989 1.03296 1.27631 1.56461 1.90802 ...
		2.31861 2.81087 3.40211 4.11317 4.96909 6] };

## Stochastic control grid
q_min = 0.; q_max = 0.05; dq = (q_max - q_min) / ( 32 - 1 );
stochastic_control_grid = { q_min : dq : q_max };
clear q_min q_max dq;

## Impulse control grid
impulse_control_grid = spatial_grid;

## Use a Neumann boundary condition $D_x u = 0$
## Useful for unbounded problems with artificial boundaries
zero_neumann = true;

## Specifies whether or not to refine grids
## Useful for bang-bang controls
stochastic_control_grid_refine = true;
impulse_control_grid_refine = true;

## Specifies whether to handle the control implicitly or explicitly
implicit = true;

