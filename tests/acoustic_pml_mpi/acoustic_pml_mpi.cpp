/**
 * This program tests serial Slightly Compressible solver with a PML
 * absorbing boundary condition.
 * A Gaussian pulse is used as the time dependent BC with max velocity
 * equal to 6cm/s.
 * The PML boundary condition (1cm long) is applied to the right boundary.
 * This test takes about 400s.
 */
#include "mpi_scnsim.h"
#include "parameters.h"
#include "utilities.h"

extern template class Fluid::MPI::SCnsIM<2>;
extern template class Fluid::MPI::SCnsIM<3>;

using namespace dealii;

int main(int argc, char *argv[])
{
  using namespace dealii;

  try
    {
      Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);

      std::string infile("parameters.prm");
      if (argc > 1)
        {
          infile = argv[1];
        }
      Parameters::AllParameters params(infile);

      double L = 1.4, H = 0.4;
      double PMLlength = 1.2, SigmaMax = 340000;

      auto sigma_pml_field =
        [PMLlength, SigmaMax](const Point<2> &p, const unsigned int component) {
          (void)component;
          double SigmaPML = 0.0;
          double boundary = 1.4;
          // For tube acoustics
          if (p[0] > boundary - PMLlength)
            {
              // A quadratic increasing function from boundary-PMLlength to the
              // boundary
              SigmaPML =
                SigmaMax * pow((p[0] + PMLlength - boundary) / PMLlength, 4);
            }
          return SigmaPML;
        };

      auto gaussian_pulse = [dt =
                               params.time_step](const Point<2> &p,
                                                 const unsigned int component,
                                                 const double time) -> double {
        auto time_value = [](double t) {
          return 6.0 * exp(-0.5 * pow((t - 0.5e-6) / 0.15e-6, 2));
        };

        if (component == 0 && std::abs(p[0]) < 1e-10)
          {
            double previous_value = time < 2 * dt ? 0.0 : time_value(time - dt);
            return time_value(time) - previous_value;
          }

        return 0.0;
      };

      if (params.dimension == 2)
        {
          parallel::distributed::Triangulation<2> tria(MPI_COMM_WORLD);
          dealii::GridGenerator::subdivided_hyper_rectangle(
            tria, {7, 2}, Point<2>(0, 0), Point<2>(L, H), true);
          Fluid::MPI::SCnsIM<2> flow(tria, params, MPI_COMM_WORLD);
          flow.add_hard_coded_boundary_condition(0, gaussian_pulse);
          flow.set_sigma_pml_field(sigma_pml_field);
          flow.run();
          auto solution = flow.get_current_solution();
          // The wave is absorbed at last, so the solution should be zero.
          auto v = solution.block(0);
          double vmax = Utils::PETScVectorMax(v);
          double verror = std::abs(vmax);
          AssertThrow(verror < 5e-2,
                      ExcMessage("Maximum velocity is incorrect!"));
        }
      else
        {
          AssertThrow(false, ExcNotImplemented());
        }
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl
                << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Exception on processing: " << std::endl
                << exc.what() << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  catch (...)
    {
      std::cerr << std::endl
                << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Unknown exception!" << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }
  return 0;
}
