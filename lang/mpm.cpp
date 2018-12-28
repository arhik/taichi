#include "tlang.h"
#include <taichi/util.h>
#include <taichi/visual/gui.h>
#include <taichi/common/bit.h>

TC_NAMESPACE_BEGIN

using namespace Tlang;

auto mpm = []() {
  bool use_adapter = true;

  constexpr int n = 128;  // grid_resolution
  const real dt = 3e-5_f, frame_dt = 1e-3_f, dx = 1.0_f / n,
             inv_dx = 1.0_f / dx;
  auto particle_mass = 1.0_f, vol = 1.0_f;
  auto hardening = 10.0_f, E = 1e4_f, nu = 0.2_f;
  real mu_0 = E / (2 * (1 + nu)), lambda_0 = E * nu / ((1 + nu) * (1 - 2 * nu));

  int dim = 2;

  Vector particle_x(dim), particle_v(dim);
  Matrix particle_F(dim, dim), particle_C(dim, dim);
  Real particle_J;

  Vector grid_v(dim);
  Real grid_m;

  Real Jp;

  int n_particles = 800;
  Program prog(Arch::x86_64, n_particles);

  prog.config.group_size = 1;
  prog.config.num_groups = 8;

  prog.layout([&]() {
    int counter = 0;
    auto place = [&](Expr &expr) {
      prog.buffer(0).range(n_particles).stream(counter++).group(0).place(expr);
    };
    for (int i = 0; i < dim; i++) {
      for (int j = 0; j < dim; j++) {
        place(particle_C(i, j));
      }
      place(particle_x(i));
      place(particle_v(i));
    }
    place(particle_J);

    prog.buffer(1).range(n * n).stream(0).group().place(grid_v(0));
    prog.buffer(1).range(n * n).stream(0).group().place(grid_v(1));
    prog.buffer(1).range(n * n).stream(0).group().place(grid_m);
    /*
    for (int k = 0; k < 2; k++) {
      prog.buffer(k).range(n * n).stream(i).group(0).place(attr[k][i]);
    }
    prog.buffer(2).range(n * n).stream(k).group(0).place(v[k]);
    */
  });

  TC_ASSERT(bit::is_power_of_two(n));

  auto p2g = prog.def([&]() {
    auto index = Expr::index(0);
    for_loop(index, {0, n_particles}, [&] {

      auto x = particle_x[index];
      auto v = particle_v[index];
      // auto F = particle_F[index];
      auto C = particle_C[index];
      auto J = particle_J[index];

      // ** gs = 2

      auto base_coord = floor(imm(inv_dx) * x - imm(0.5_f));
      auto fx = x * imm(inv_dx) - base_coord;

      Vector w[3];
      w[0] = imm(0.5_f) * sqr(imm(1.5_f) - fx);
      w[1] = imm(0.75_f) - sqr(fx - imm(1.0_f));
      w[2] = imm(0.5_f) * sqr(fx - imm(0.5_f));

      // auto J = F(0, 0) * F(1, 1) - F(1, 0) * F(0, 1);
      auto base_offset =
          cast<int>(base_coord(0)) * imm(n) + cast<int>(base_coord(1));

      // scatter
      for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
          auto weight = w[i](0) * w[j](1);
          auto node = base_offset + imm(i * n + j);
          grid_v[node] = grid_v[node] + imm(particle_mass) * weight * v;
          grid_m[node] = grid_m[node] + imm(particle_mass) * weight;
        }
      }

      // ** gs = 1
      /*
      auto offset =
          cast<int32>(offset_x) * imm(n) + cast<int32>(offset_y) * imm(1);

      auto clamp = [](const Expr &e) {
        return min(max(imm(2), e), imm(n - 2));
      };

      // weights
      auto w00 = (imm(1.0f) - wx) * (imm(1.0f) - wy);
      auto w01 = (imm(1.0f) - wx) * wy;
      auto w10 = wx * (imm(1.0f) - wy);
      auto w11 = wx * wy;

      w00.name("w00");
      w01.name("w01");
      w10.name("w10");
      w11.name("w11");

      Expr node = max(Expr::index(0) + offset, imm(0));
      Int32 i = clamp(node >> imm((int)bit::log2int(n))).name("i");  // node / n
      // Int32 i = clamp(node / imm(n)).name("i"); // node / n
      Int32 j = clamp(node & imm(n - 1)).name("j");  // node % n
      // Int32 j = clamp(node % imm(n)).name("j"); // node % n
      node = i * imm(n) + j;
      node.name("node");

      if (use_adapter) {
        prog.adapter(2).set(1, 4).convert(w00, w01, w10, w11);
        prog.adapter(3).set(1, 4).convert(node);
      }

      // ** gs = 4
      for (int k = 0; k < nattr; k++) {
        auto v00 = attr[0][k][node + imm(0)].name("v00");
        auto v01 = attr[0][k][node + imm(1)].name("v01");
        auto v10 = attr[0][k][node + imm(n)].name("v10");
        auto v11 = attr[0][k][node + imm(n + 1)].name("v11");

        attr[1][k][index] = w00 * v00 + w01 * v01 + w10 * v10 + w11 * v11;
        // attr[1][k][index] = w00 * v00;
        attr[1][k][index].name(fmt::format("output{}", k));
      }
      */
    });
  });

  p2g();

  auto grid_op = [&]() {
    for (int i = 0; i < n; i++) {
      for (int j = 0; j < n; j++) {
        auto &v0 = prog.data(grid_v(0), i * n + j);
        auto &v1 = prog.data(grid_v(1), i * n + j);
        auto &m = prog.data(grid_m, i * n + j);
        if (m > 0) {
          v0 /= m;
          v1 /= m;
        }
        if (j < 5) {
          v0 = 0;
          v1 = 0;
        }
      }
    }
  };

  auto g2p = []() {

  };

  int scale = 4;
  GUI gui("Advection", n * 4, n * 4);

  for (int f = 0; f < 1000; f++) {
    for (int t = 0; t < 3; t++) {
      TC_TIME(p2g());
      TC_TIME(grid_op());
      TC_TIME(g2p());

      for (int i = 0; i < n * scale; i++) {
        for (int j = 0; j < n * scale; j++) {
          gui.buffer[i][j].x =
              prog.data(grid_v(0), i / scale * n + j / scale) + 0.5;
          gui.buffer[i][j].y =
              prog.data(grid_v(1), i / scale * n + j / scale) + 0.5;
        }
      }

      // prog.swap_buffers(0, 1);
    }

    gui.update();
    // gui.screenshot(fmt::format("images/{:04d}.png", f));
  }
};
TC_REGISTER_TASK(mpm);

TC_NAMESPACE_END

/*
TODO: arbitrary for loop (bounds using arbitrary constants)
 */
