#ifndef CSLIBS_NDT_2D_MATCHING_NLOPT_OCCUPANCY_GRIDMAP_FUNCTION_HPP
#define CSLIBS_NDT_2D_MATCHING_NLOPT_OCCUPANCY_GRIDMAP_FUNCTION_HPP

#include <cslibs_ndt/matching/nlopt/function.hpp>
#include <cslibs_ndt/map/map.hpp>

namespace cslibs_ndt {
namespace matching {
namespace nlopt {

template <cslibs_ndt::map::tags::option option_t,
          typename _T,
          template <typename, typename, typename...> class backend_t,
          template <typename, typename, typename...> class dynamic_backend_t,
          typename point_t>
class Function<
        cslibs_ndt::map::Map<option_t,2,cslibs_ndt::OccupancyDistribution,_T,backend_t,dynamic_backend_t>,
        point_t> {
public:
    using ndt_t = cslibs_ndt::map::Map<option_t,2,cslibs_ndt::OccupancyDistribution,_T,backend_t,dynamic_backend_t>;

    struct Functor {
        const ndt_t* map_;
        const std::vector<point_t>* points_;
        const typename ndt_t::inverse_sensor_model_t::Ptr* ivm_;

        std::array<double,3> initial_guess_;
        double translation_weight_;
        double rotation_weight_;
        double map_weight_;
    };

    inline static double apply(unsigned n, const double *x, double *grad, void* ptr)
    {
        const Functor& object = *((Functor*)ptr);
        const auto& points    = *(object.points_);
        const auto& map       = *(object.map_);
        const auto& ivm       = *(object.ivm_);
        const auto& orig_inv  = map.getInitialOrigin().inverse();

        double fi = 0;
        if (grad) {
          for (int i=0; i<3; ++i)
            grad[i] = 0;
        }

        const typename ndt_t::pose_t current_transform(x[0],x[1],x[2]);
        const auto& s = current_transform.sin();
        const auto& c = current_transform.cos();

        std::size_t count = 0;
        for (const auto& p : points) {
            const typename ndt_t::point_t q = current_transform * typename ndt_t::point_t(p(0),p(1));
            //++count; //--> ATE: 2.9094, RPE: 0.0142 (thr. 0.3)
            //         //--> ATE: 2.9094, RPE: 0.0142 (thr. 0.0)
            const auto& bundle = map.get(q);
            if (!bundle)
                continue;
            ++count;

            const typename ndt_t::point_t q_prime = orig_inv * q;
            for (std::size_t i=0; i<4; ++i) {
                if (const auto& bi = bundle->at(i)) {
                    if (const auto& di = bi->getDistribution()) {
                        //++count; //--> ATE: 2.7647, RPE: 0.0139 (thr. 0.3)
                        //         //--> ATE: 2.9947, RPE: 0.0142 (thr. 0.0)
                        if (!di->valid())
                            continue;
                        //++count; //--> ATE: 3.0394, RPE: 0.0123 (thr. 0.3)
                        //         //--> ATE: 2.6005 RPE: 0.0150 (thr. 0.0)

                        const auto& mean = di->getMean();
                        const auto inf = di->getInformationMatrix();

                        const double occ = bi->getOccupancy(ivm);
                        const auto& qm = q_prime.data() - mean;
                        const auto& qm_inf = qm.transpose() * inf;
                        const auto& exponent = -0.5 * qm_inf * qm;
                        const double score = 0.25 * std::exp(exponent) * occ;
                        fi -= score;

                        if (grad) {
                          std::array<Eigen::Matrix<_T,2,1>,3> d;
                          d[0] << 1, 0;
                          d[1] << 0, 1;
                          d[2] << -s*qm(0)-c*qm(1), c*qm(0)-s*qm(1);
                          grad[0] += score * qm_inf * d[0];
                          grad[1] += score * qm_inf * d[1];
                          grad[2] += score * qm_inf * d[2];
                        }
                    }
                }
            }
        }

        fi /= static_cast<double>(count);
        if (grad) {
          for (int i=0; i<3; ++i)
            grad[i] /= static_cast<double>(count);
        } else {
            const auto initial_guess = (object.initial_guess_);
            const double trans_diff = hypot(x[0] - initial_guess[0], x[1] - initial_guess[1]);
            const double rot_diff = std::fabs(cslibs_math::common::angle::difference(x[2], initial_guess[2]));
            return (object.map_weight_)/* * points.size()*/ * fi + (object.translation_weight_) * trans_diff + (object.rotation_weight_) * rot_diff;
        }
        return fi;
    }
};

}
}
}

#endif // CSLIBS_NDT_2D_MATCHING_NLOPT_OCCUPANCY_GRIDMAP_FUNCTION_HPP
