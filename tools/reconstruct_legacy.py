from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=root/'portfolio.hpp'
s=p.read_text(encoding='utf-8')
klass=s[s.index('class SlsqpSolver {'):s.index('struct PortfolioResult')]
constraints=s[s.index('using Constraints='):s.index('struct SolveInfo {')]
diagnostics=s[s.index('inline void fill_constraint_diagnostics'):s.index('inline SolveInfo solve_convex_weights')]
header='''// Compatibility-only SLSQP. The convex primary solver is in solver.cpp.
#include "mvo/compatibility.hpp"
#include <numeric>
#include <array>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4505 4702 4701)
#endif
#define nlopt_isinf std::isinf
#define nlopt_isfinite std::isfinite
#include "../slsqp_core.hpp"
#undef nlopt_isinf
#undef nlopt_isfinite
#ifdef _MSC_VER
#pragma warning(pop)
#endif
namespace mvo::legacy {
constexpr int N=8, M=18;
using Row=std::array<double,8>;
using Matrix=std::array<Row,8>;
struct Statistics {Row mean{},stddev{},variance{};Matrix cov{};};
enum class SolverMode {Compatible,Analytic};
struct Options {int max_iterations=1000;double ftol=1e-6;std::optional<double> risk_averse;bool ewm=false;SolverMode solver=SolverMode::Compatible;};
struct SolveInfo {Row weight{};int status=0,iterations=0,evaluations=0;double objective=0,max_violation=0,sum_weight_error=0,turnover_error=0,group_violation=0;bool fallback=false,retry=false,success=false;std::string message;};
'''
footer='''
} // namespace mvo::legacy
namespace mvo {
SolveInfo solve_compatibility(const MomentEstimate& m,const Vector& previous,std::optional<double> risk,bool ewm) {
    legacy::Statistics stats;legacy::Row p;
    for(int i=0;i<8;++i){p[i]=previous[i];stats.mean[i]=m.mean[i];stats.stddev[i]=m.volatility[i];stats.variance[i]=m.covariance(i,i);for(int j=0;j<8;++j)stats.cov[i][j]=m.covariance(i,j);}
    legacy::Options options;options.risk_averse=risk;options.ewm=ewm;legacy::SlsqpSolver solver;auto solved=solver.solve(stats,p,options);
    SolveInfo result;for(int i=0;i<8;++i)result.weight[i]=solved.weight[i];result.solver=SolverKind::SLSQP;result.success=solved.success;result.fallback=solved.fallback;result.retry=solved.retry;result.iterations=solved.iterations;result.objective=solved.objective;result.constraints=validate_weight(result.weight,previous);result.primal_residual=result.constraints.max_violation();result.message=solved.message+"; legacy finite-difference tolerance 1e-6; dual/KKT unavailable";return result;
}
}
'''
(root/'src/compatibility.cpp').write_text(header+constraints+diagnostics+klass+footer,encoding='utf-8')
print('Compatibility solver isolated')
