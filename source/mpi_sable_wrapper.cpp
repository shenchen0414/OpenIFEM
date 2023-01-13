#include "mpi_sable_wrapper.h"

namespace Fluid
{

  namespace MPI
  {

    template <int dim>
    SableWrap<dim>::SableWrap(parallel::distributed::Triangulation<dim> &tria,
                              const Parameters::AllParameters &parameters,
                              std::vector<int> &sable_ids,
                              MPI_Comm m)
      : FluidSolver<dim>(tria, parameters, m), sable_ids(sable_ids)
    {
      AssertThrow(
        parameters.fluid_velocity_degree == parameters.fluid_pressure_degree ==
          1,
        ExcMessage("Use 1st order elements for both pressure and velocity!"));
    }

    template <int dim>
    void SableWrap<dim>::run()
    {

      setup_dofs();
      initialize_system();

      while (is_comm_active)
        {
          if (time.current() == 0)
            run_one_step();
          get_dt_sable();
          run_one_step();
        }
    }

    template <int dim>
    void SableWrap<dim>::run_one_step(bool apply_nonzero_constraints,
                                      bool assemble_system)
    {

      if (time.get_timestep() == 0)
        {

          sable_no_nodes_one_dir = 0;
          sable_no_ele = 0;
          sable_no_nodes = 0;
          Max(sable_no_nodes_one_dir);
          Max(sable_no_ele);
          Max(sable_no_nodes);

          find_ghost_nodes();

          output_results(0);
        }
      else
        {
          // Recieve no. of nodes and elements from Sable
          sable_no_nodes_one_dir = 0;
          sable_no_ele = 0;
          sable_no_nodes = 0;
          Max(sable_no_nodes_one_dir);
          Max(sable_no_ele);
          Max(sable_no_nodes);

          is_comm_active = All(is_comm_active);
          std::cout << std::string(96, '*') << std::endl
                    << "Received solution from Sable at time step = "
                    << time.get_timestep() << ", at t = " << std::scientific
                    << time.current() << std::endl;
          // Output
          if ((int(time.get_timestep()) % int(parameters.output_interval)) == 0)
            {
              output_results(time.get_timestep());
            }
        }
    }

    template <int dim>
    bool SableWrap<dim>::All(bool my_b)
    {
      int ib = (my_b == true ? 0 : 1);
      int result = 0;
      MPI_Allreduce(&ib, &result, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
      return (result == 0);
    }

    template <int dim>
    void SableWrap<dim>::get_dt_sable()
    {
      double dt = 0;
      Max(dt);
      time.set_delta_t(dt);
      time.increment();
    }

    template <int dim>
    void SableWrap<dim>::Max(int &send_buffer)
    {
      int temp;
      MPI_Allreduce(&send_buffer, &temp, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
      send_buffer = temp;
    }

    template <int dim>
    void SableWrap<dim>::Max(double &send_buffer)
    {
      double temp;
      MPI_Allreduce(
        &send_buffer, &temp, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
      send_buffer = temp;
    }

    template <int dim>
    void SableWrap<dim>::rec_data(double **rec_buffer,
                                  const std::vector<int> &cmapp,
                                  const std::vector<int> &cmapp_sizes,
                                  int data_size)
    {
      std::vector<MPI_Request> handles;
      for (unsigned ict = 0; ict < cmapp.size(); ict++)
        {
          MPI_Request req;
          MPI_Irecv(rec_buffer[ict],
                    cmapp_sizes[ict],
                    MPI_DOUBLE,
                    cmapp[ict],
                    1,
                    MPI_COMM_WORLD,
                    &req);
          handles.push_back(req);
        }
      std::vector<MPI_Request>::iterator hit;
      for (hit = handles.begin(); hit != handles.end(); hit++)
        {
          MPI_Status stat;
          MPI_Wait(&(*hit), &stat);
        }
    }

    template <int dim>
    void SableWrap<dim>::send_data(double **send_buffer,
                                   const std::vector<int> &cmapp,
                                   const std::vector<int> &cmapp_sizes)
    {
      for (unsigned ict = 0; ict < cmapp.size(); ict++)
        {
          int status = MPI_Send(send_buffer[ict],
                                cmapp_sizes[ict],
                                MPI_DOUBLE,
                                cmapp[ict],
                                1,
                                MPI_COMM_WORLD);
          assert(status == MPI_SUCCESS);
        }
    }

    template <int dim>
    void SableWrap<dim>::find_ghost_nodes()
    {

      int node_z =
        int(sable_no_nodes / (sable_no_nodes_one_dir * sable_no_nodes_one_dir));
      int node_z_begin = 0;
      int node_z_end = node_z;

      int sable_no_el_one_dir = (dim == 2 ? int(std::sqrt(sable_no_ele))
                                          : int(std::cbrt(sable_no_ele)));

      int ele_z =
        int(sable_no_ele / (sable_no_el_one_dir * sable_no_el_one_dir));
      int ele_z_begin = 0;
      int ele_z_end = ele_z;

      if (dim == 3)
        {
          node_z_begin = 1;
          node_z_end = node_z - 1;

          ele_z_begin = 1;
          ele_z_end = ele_z - 1;
        }

      for (int l = node_z_begin; l < node_z_end; l++)
        {
          int cornder_node_id =
            l * sable_no_nodes_one_dir * sable_no_nodes_one_dir +
            sable_no_nodes_one_dir + 1;
          for (int i = 0; i < sable_no_nodes_one_dir - 2; i++)
            {
              for (int j = 0; j < sable_no_nodes_one_dir - 2; j++)
                {
                  int n = cornder_node_id + j + i * (sable_no_nodes_one_dir);
                  non_ghost_nodes.push_back(n);
                }
            }
        }

      for (int l = ele_z_begin; l < ele_z_end; l++)
        {
          int cornder_el_id = l * sable_no_el_one_dir * sable_no_el_one_dir +
                              sable_no_el_one_dir + 1;
          for (int i = 0; i < sable_no_el_one_dir - 2; i++)
            {
              for (int j = 0; j < sable_no_el_one_dir - 2; j++)
                {
                  int n = cornder_el_id + j + i * (sable_no_el_one_dir);
                  non_ghost_cells.push_back(n);
                }
            }
        }

      assert(non_ghost_nodes.size() == triangulation.n_vertices());
      assert(non_ghost_cells.size() == triangulation.n_cells());
    }

    template class SableWrap<2>;
    template class SableWrap<3>;
  } // namespace MPI
} // namespace Fluid
