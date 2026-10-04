#pragma once
#include "xls_reader.hpp"
#include "convex_solver.hpp"
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <numeric>
#include <optional>
#include <ostream>
#include <utility>

#define nlopt_isinf std::isinf
#define nlopt_isfinite std::isfinite
#include "slsqp_core.hpp"
#undef nlopt_isinf
#undef nlopt_isfinite

namespace mvo {
constexpr int N=8, M=18;
using Matrix=std::array<Row,N>;
inline const Row initial_weights={8.9/100,10.25/100,6.35/100,6.45/100,25.72/100,16.55/100,12.72/100,13.06/100};
inline double nan(){return std::numeric_limits<double>::quiet_NaN();}
inline double divide(double a,double b){return b!=0?a/b:(a==0?nan():std::copysign(std::numeric_limits<double>::infinity(),a));}

enum class AccuracyMode { PythonCompatible, HighAccuracy };
enum class InputType { CumulativePnL, NAV, DailyReturn };
enum class MissingValuePolicy { Reject, DropRow };
enum class SolverKind { LP, QP, SLSQP };
inline std::string date_string(double serial){
    // Excel 1900 leap-year compatibility; 1904 workbooks are normalized by reader.
    int z=int(std::floor(serial));int days=z-(z>=60?2:1),y=1900,m=1;
    auto leap=[](int y){return y%4==0 && (y%100!=0 || y%400==0);};
    while(days>=365+leap(y)){days-=365+leap(y);++y;}
    int md[]={31,28+int(leap(y)),31,30,31,30,31,31,30,31,30,31};
    while(days>=md[m-1]){days-=md[m-1];++m;}
    char buf[16];std::snprintf(buf,sizeof(buf),"%04d%02d%02d",y,m,days+1);return buf;
}

struct Metrics {double annual_return,volatility,sharpe,calmar,win_percent,max_drawdown;size_t peak,trough;};
class EvaluatePNL {
    const std::vector<double>& pnl_; const std::vector<double>& dates_; int yr_dates_;
public:
    EvaluatePNL(const std::vector<double>& pnl,const std::vector<double>& dates,int yr_dates=250):pnl_(pnl),dates_(dates),yr_dates_(yr_dates){
        if(pnl.size()!=dates.size() || pnl.size()<2 || yr_dates<=0)throw std::invalid_argument("invalid PNL/dates/annual days");
    }
    std::pair<double,std::pair<size_t,size_t>> get_max_drawdown() const {
        // Keep the earliest (left,right) pair for ties, like np.triu_indices/argmax.
        double peak=pnl_[0],best=-std::numeric_limits<double>::infinity();size_t pi=0,li=0,ri=1;
        for(size_t j=1;j<pnl_.size();++j){double dd=peak-pnl_[j];if(dd>best || (dd==best && (pi<li || (pi==li && j<ri)))){best=dd;li=pi;ri=j;}if(pnl_[j]>peak){peak=pnl_[j];pi=j;}}
        return {best,{li,ri}};
    }
    double get_max_drawdown(bool get_date) const {
        (void)get_date;
        return get_max_drawdown().first;
    }
    std::pair<std::string,std::string> max_drawdown_date() const {
        auto d=get_max_drawdown(); return {date_string(dates_[d.second.first]),date_string(dates_[d.second.second])};
    }
    Metrics evaluate() const {
        // Zero return filtering happens after cumsum+1, as in the Python code.
        double sum=0;size_t count=0,wins=0;
        for(size_t i=1;i<pnl_.size();++i){double d=pnl_[i]-pnl_[i-1];if(d!=0){sum+=d;++count;wins+=d>0;}}
        double mean=count?sum/count:nan(),ss=0;
        for(size_t i=1;i<pnl_.size();++i){double d=pnl_[i]-pnl_[i-1];if(d!=0)ss+=(d-mean)*(d-mean);}
        double rt=mean*yr_dates_,vol=count>1?std::sqrt(ss/(count-1))*std::sqrt(double(yr_dates_)):nan();
        auto dd=get_max_drawdown();double yrs=(dates_.back()-dates_.front())/365.;
        return {rt,vol,divide(rt,vol),divide(pnl_.back()-pnl_.front(),yrs*dd.first),count?double(wins)/count:nan(),dd.first,dd.second.first,dd.second.second};
    }
    double get_return() const {return evaluate().annual_return;}
    double get_std() const {return evaluate().volatility;}
    double get_sharp() const {return evaluate().sharpe;}
    double get_win_percent() const {return evaluate().win_percent;}
    double get_calmar() const {return evaluate().calmar;}
    void test_pipline(std::ostream& os) const {
        auto v=evaluate();os<<std::fixed<<std::setprecision(2)<<v.annual_return*100<<" & "<<v.volatility*100<<" & "<<std::setprecision(3)<<v.sharpe<<" & "<<v.calmar<<" & "<<std::setprecision(2)<<v.win_percent*100<<" & "<<v.max_drawdown*100<<" & "<<date_string(dates_[v.peak])<<" & "<<date_string(dates_[v.trough])<<'\n';
    }
};

inline Series daily_returns(const Series& values,
                            InputType input_type=InputType::CumulativePnL,
                            MissingValuePolicy missing=MissingValuePolicy::Reject){
    if(values.empty()) throw std::invalid_argument("input series is empty");
    Series r(values.size());
    for(size_t i=0;i<values.size();++i){
        for(int j=0;j<N;++j){
            if(!std::isfinite(values[i][j])){
                if(missing==MissingValuePolicy::Reject) throw std::invalid_argument("input contains NaN or Inf");
                r[i][j]=0.0;
                continue;
            }
            if(i==0){r[i][j]=0.0;continue;}
            if(!std::isfinite(values[i-1][j])){
                if(missing==MissingValuePolicy::Reject) throw std::invalid_argument("input contains NaN or Inf");
                r[i][j]=0.0;continue;
            }
            double d=0.0;
            if(input_type==InputType::CumulativePnL) d=values[i][j]-values[i-1][j];
            else if(input_type==InputType::NAV){
                if(values[i-1][j]==0.0) throw std::invalid_argument("NAV contains zero denominator");
                d=values[i][j]/values[i-1][j]-1.0;
            } else d=values[i][j];
            if(!std::isfinite(d)) throw std::invalid_argument("daily data contains NaN or Inf");
            r[i][j]=d;
        }
    }
    return r;
}
inline Series norm_preprocess(const Series& input,int window=120,double n_sigma=3){
    if(window<2 || !std::isfinite(n_sigma) || n_sigma<0)throw std::invalid_argument("window>=2, sigma>=0 required");
    Series out=input; if(input.size()<=size_t(window))return out;
    // O(T*N), sequential clipping uses previously clipped observations.
    // Long double running moments limit cancellation versus repeated scans.
    std::array<long double,N> sum{},sq{};
    auto update=[&](const Row& r,int sign){for(int j=0;j<N;++j){sum[j]+=sign*(long double)r[j];sq[j]+=sign*(long double)r[j]*r[j];}};
    for(int i=0;i<window;++i)update(out[i],1);
    for(size_t i=window;i<out.size();++i){for(int j=0;j<N;++j){double mean=double(sum[j]/window);double sd=std::sqrt(double(std::max(0.L,(sq[j]-sum[j]*sum[j]/window)/(window-1))));out[i][j]=std::clamp(out[i][j],mean-n_sigma*sd,mean+n_sigma*sd);}update(out[i-window],-1);update(out[i],1);}
    return out;
}
struct Statistics {
    Row mean{},stddev{},variance{};
    Matrix cov{};
    double shrinkage_alpha=0.0;
    double min_eigen_before=0.0;
    double min_eigen_after=0.0;
    double condition_number=1.0;
};

inline void symmetrize(Matrix& a){for(int i=0;i<N;++i)for(int j=i+1;j<N;++j){double v=.5*(a[i][j]+a[j][i]);a[i][j]=a[j][i]=v;}}

inline void jacobi_eigen(const Matrix& input,Matrix& q,Row& eigen){
    Matrix a=input;for(int i=0;i<N;++i)for(int j=0;j<N;++j)q[i][j]=i==j?1.0:0.0;
    for(int iteration=0;iteration<128;++iteration){
        int p=0,r=1;double largest=0.0;
        for(int i=0;i<N;++i)for(int j=i+1;j<N;++j)if(std::abs(a[i][j])>largest){largest=std::abs(a[i][j]);p=i;r=j;}
        if(largest<=1e-15)break;
        double angle=.5*std::atan2(2.0*a[p][r],a[r][r]-a[p][p]);double c=std::cos(angle),s=std::sin(angle);
        for(int k=0;k<N;++k)if(k!=p && k!=r){double akp=a[k][p],akr=a[k][r];a[k][p]=a[p][k]=c*akp-s*akr;a[k][r]=a[r][k]=s*akp+c*akr;}
        double app=a[p][p],arr=a[r][r],apr=a[p][r];a[p][p]=c*c*app-2*s*c*apr+s*s*arr;a[r][r]=s*s*app+2*s*c*apr+c*c*arr;a[p][r]=a[r][p]=0.0;
        for(int k=0;k<N;++k){double qkp=q[k][p],qkr=q[k][r];q[k][p]=c*qkp-s*qkr;q[k][r]=s*qkp+c*qkr;}
    }
    for(int i=0;i<N;++i)eigen[i]=a[i][i];
}

inline void repair_psd(Matrix& cov,Statistics& out){
    symmetrize(cov);Matrix q{};Row eigen{};jacobi_eigen(cov,q,eigen);
    out.min_eigen_before=*std::min_element(eigen.begin(),eigen.end());double largest=*std::max_element(eigen.begin(),eigen.end());double floor=1e-12*std::max(1.0,largest);
    for(double& value:eigen)value=std::max(value,floor);out.min_eigen_after=*std::min_element(eigen.begin(),eigen.end());
    Matrix repaired{};for(int i=0;i<N;++i)for(int j=0;j<N;++j)for(int k=0;k<N;++k)repaired[i][j]+=q[i][k]*eigen[k]*q[j][k];symmetrize(repaired);cov=repaired;
    out.condition_number=largest>floor?largest/floor:1.0;
}

inline void ledoit_wolf_shrink(Matrix& cov,const Series& x,size_t begin,size_t end,const Row& mean,Statistics& out){
    double trace=0.0;for(int i=0;i<N;++i)trace+=cov[i][i];double mu=trace/N;
    double gamma=0.0;for(int i=0;i<N;++i)for(int j=0;j<N;++j){double target_ij=i==j?mu:0.0;double d=cov[i][j]-target_ij;gamma+=d*d;}
    double phi=0.0;size_t count=end-begin;for(size_t t=begin;t<end;++t){Row z{};for(int i=0;i<N;++i)z[i]=x[t][i]-mean[i];for(int i=0;i<N;++i)for(int j=0;j<N;++j){double d=z[i]*z[j]-cov[i][j];phi+=d*d;}}
    double alpha=gamma>1e-30?std::clamp(phi/(std::max<size_t>(1,count)*gamma),0.0,1.0):1.0;out.shrinkage_alpha=alpha;
    for(int i=0;i<N;++i)for(int j=0;j<N;++j)cov[i][j]=(1-alpha)*cov[i][j]+alpha*(i==j?mu:0.0);
}

inline Statistics statistics(const Series& x,size_t begin,size_t end,int span,bool ewm,AccuracyMode mode=AccuracyMode::PythonCompatible){
    if(end> x.size() || end-begin<2)throw std::invalid_argument("need >=2 observations for covariance");
    Statistics s;size_t count=end-begin;
    if(!ewm){
        for(size_t t=begin;t<end;++t)for(int j=0;j<N;++j)s.mean[j]+=x[t][j];
        for(double& v:s.mean)v/=count;
        for(size_t t=begin;t<end;++t)for(int a=0;a<N;++a)for(int b=0;b<=a;++b)s.cov[a][b]+=(x[t][a]-s.mean[a])*(x[t][b]-s.mean[b]);
        for(int a=0;a<N;++a)for(int b=0;b<=a;++b)s.cov[b][a]=s.cov[a][b]/=count-1;
    }else{
        double alpha=2.0/(span+1),decay=1-alpha;s.mean=x[begin];double sumw2=1;
        for(size_t t=begin+1;t<end;++t){Row old=s.mean;for(int j=0;j<N;++j)s.mean[j]=decay*old[j]+alpha*x[t][j];for(int a=0;a<N;++a)for(int b=0;b<=a;++b)s.cov[a][b]=decay*(s.cov[a][b]+alpha*(x[t][a]-old[a])*(x[t][b]-old[b]));sumw2=decay*decay*sumw2+alpha*alpha;}
        for(int a=0;a<N;++a)for(int b=0;b<=a;++b)s.cov[b][a]=s.cov[a][b]/=(1-sumw2);
    }
    if(mode==AccuracyMode::HighAccuracy){ledoit_wolf_shrink(s.cov,x,begin,end,s.mean,s);repair_psd(s.cov,s);}
    for(int j=0;j<N;++j){s.variance[j]=std::max(0.0,s.cov[j][j]);s.stddev[j]=std::sqrt(s.variance[j]);}
    return s;
}

inline Statistics statistics_hac(const Series& x,size_t begin,size_t end,int horizon,bool ewm,AccuracyMode mode){
    if(end>x.size() || end-begin<2)throw std::invalid_argument("need >=2 observations for covariance");
    if(mode==AccuracyMode::PythonCompatible)return statistics(x,begin,end,horizon,ewm,mode);
    Statistics s;const size_t count=end-begin;const int h=std::max(1,std::min<int>(horizon,static_cast<int>(count)-1));
    std::vector<double> weights(count);double alpha=2.0/(horizon+1),decay=1-alpha,weight_sum=0.0;
    for(size_t t=begin;t<end;++t){weights[t-begin]=ewm?(t==begin?std::pow(decay,double(count-1)):alpha*std::pow(decay,double(end-1-t))):1.0;weight_sum+=weights[t-begin];}
    for(size_t t=begin;t<end;++t)for(int j=0;j<N;++j)s.mean[j]+=weights[t-begin]*x[t][j]/weight_sum;
    for(int lag=0;lag<h;++lag){double lag_weight=double(h-lag);double pair_sum=0.0;for(size_t t=begin+lag;t<end;++t){double wt=lag==0?weights[t-begin]:std::sqrt(weights[t-begin]*weights[t-lag-begin]);pair_sum+=wt;Row z1{},z2{};for(int j=0;j<N;++j){z1[j]=x[t][j]-s.mean[j];z2[j]=x[t-lag][j]-s.mean[j];}for(int a=0;a<N;++a)for(int b=0;b<=a;++b){s.cov[a][b]+=lag_weight*wt*z1[a]*z2[b];if(lag>0)s.cov[a][b]+=lag_weight*wt*z2[a]*z1[b];}}
        if(pair_sum>0)for(int a=0;a<N;++a)for(int b=0;b<=a;++b)s.cov[a][b]/=pair_sum;
    }
    for(int a=0;a<N;++a)for(int b=0;b<=a;++b)s.cov[b][a]=s.cov[a][b];
    ledoit_wolf_shrink(s.cov,x,begin,end,s.mean,s);repair_psd(s.cov,s);
    for(int j=0;j<N;++j)s.mean[j]*=h;
    for(int j=0;j<N;++j){s.variance[j]=std::max(0.0,s.cov[j][j]);s.stddev[j]=std::sqrt(s.variance[j]);}
    return s;
}
struct Rebalance {size_t day;Statistics equal,ewm;};
struct PreparedData {Series processed;std::vector<Rebalance> schedule;AccuracyMode mode=AccuracyMode::PythonCompatible;};
inline PreparedData prepare(const Series& returns,int window,int keep,double sigma=3,AccuracyMode mode=AccuracyMode::PythonCompatible){
    if(window<2 || keep<1 || keep>=window)throw std::invalid_argument("require 1<=keep<window");
    PreparedData p;p.mode=mode;p.processed=norm_preprocess(returns,window,sigma);
    if(returns.size()<=size_t(window))return p;
    if(mode==AccuracyMode::HighAccuracy){
        for(size_t i=window;i<returns.size();i+=keep)p.schedule.push_back({i,statistics_hac(p.processed,i-window,i,keep,false,mode),statistics_hac(p.processed,i-window,i,keep,true,mode)});
        return p;
    }
    // One rolling sum for all scenarios; reset at each rebalance for stable local sums.
    Series rolled;rolled.reserve(window-keep+1);
    for(size_t i=window;i<returns.size();i+=keep){rolled.clear();Row sum{};std::array<long double,N> acc{};
        for(size_t t=i-window;t<i;++t){for(int j=0;j<N;++j){acc[j]+=p.processed[t][j];if(t>=i-window+keep)acc[j]-=p.processed[t-keep][j];sum[j]=double(acc[j]);}if(t>=i-window+keep-1)rolled.push_back(sum);}
        p.schedule.push_back({i,statistics(rolled,0,rolled.size(),window,false),statistics(rolled,0,rolled.size(),window,true)});
    }return p;
}

enum class SolverMode {Compatible,Analytic};
struct Options {int window=120,keep=1,yr_dates=250,max_iterations=1000;double sigma=3,ftol=1e-6;Row init_weight=initial_weights;std::optional<double> risk_averse;bool ewm=false;SolverMode solver=SolverMode::Compatible;AccuracyMode accuracy=AccuracyMode::PythonCompatible;};
using Constraints=std::array<double,M>;
inline Constraints get_constrain(const Row& w,const Row& prev){
    Constraints c{};c[0]=std::accumulate(w.begin(),w.end(),0.0)-1;for(int j=0;j<N;++j)c[1+j]=w[j];double turnover=0;for(int j=0;j<N;++j)turnover+=std::abs(w[j]-prev[j]);c[9]=.1-turnover;
    double g=w[0]+w[1]+w[2]+w[3];c[10]=g-.15;c[11]=.35-g;c[12]=w[4]-.15;c[13]=.35-w[4];c[14]=w[5]-.15;c[15]=.35-w[5];g=w[6]+w[7];c[16]=g-.15;c[17]=.35-g;return c;
}
inline double violation(const Row& w,const Row& prev){
    for(double v:w)if(!std::isfinite(v))return std::numeric_limits<double>::infinity();
    auto c=get_constrain(w,prev);double v=std::abs(c[0]);for(int i=1;i<M;++i)v=std::max(v,-c[i]);return v;
}
struct SolveInfo {
    Row weight{};
    int status=0,iterations=0,evaluations=0;
    double objective=0,max_violation=0,primal_residual=0,dual_residual=0,kkt_residual=0;
    double sum_weight_error=0,turnover_error=0,group_violation=0;
    SolverKind solver_kind=SolverKind::SLSQP;
    bool success=false,fallback=false,retry=false;
    std::string message;
};

struct WeightConvexModel {
    std::vector<std::vector<double>> h,a,equality;
    std::vector<double> f,b,equality_rhs,initial;
    std::vector<int> initial_active;
};

inline int add_inequality(WeightConvexModel& model,const std::vector<double>& row,double rhs){model.a.push_back(row);model.b.push_back(rhs);return static_cast<int>(model.a.size()-1);}
inline std::vector<double> negate_row(std::vector<double> row){for(double& value:row)value=-value;return row;}

inline WeightConvexModel make_weight_convex_model(const Statistics& s,const Row& previous,const Options& options){
    constexpr int n=2*N;WeightConvexModel model;model.h.assign(n,std::vector<double>(n,0.0));model.f.assign(n,0.0);model.initial.assign(n,0.0);
    const double ridge=1e-12;
    for(int i=0;i<N;++i){model.initial[i]=previous[i];model.initial[N+i]=0.0;model.f[i]=options.risk_averse? -s.mean[i]:-s.mean[i];for(int j=0;j<N;++j)if(options.risk_averse)model.h[i][j]=*options.risk_averse*s.cov[i][j];model.h[i][i]+=ridge;model.h[N+i][N+i]=ridge;}
    std::vector<double> sum_w(n,0.0);for(int i=0;i<N;++i)sum_w[i]=1.0;model.equality.push_back(sum_w);model.equality_rhs.push_back(1.0);
    for(int i=0;i<N;++i){std::vector<double> row(n,0.0);row[i]=-1.0;add_inequality(model,row,0.0);row.assign(n,0.0);row[N+i]=-1.0;model.initial_active.push_back(add_inequality(model,row,0.0));row.assign(n,0.0);row[N+i]=1.0;add_inequality(model,row,.1);}
    for(int i=0;i<N;++i){std::vector<double> row(n,0.0);row[i]=1.0;row[N+i]=-1.0;add_inequality(model,row,previous[i]);row.assign(n,0.0);row[i]=-1.0;row[N+i]=-1.0;add_inequality(model,row,-previous[i]);}
    std::vector<double> turnover(n,0.0);for(int i=0;i<N;++i)turnover[N+i]=1.0;add_inequality(model,turnover,.1);
    std::vector<double> group(n,0.0);for(int i=0;i<4;++i)group[i]=1.0;add_inequality(model,negate_row(group),-.15);add_inequality(model,group,.35);
    group.assign(n,0.0);group[4]=1.0;add_inequality(model,negate_row(group),-.15);add_inequality(model,group,.35);
    group.assign(n,0.0);group[5]=1.0;add_inequality(model,negate_row(group),-.15);add_inequality(model,group,.35);
    group.assign(n,0.0);group[6]=group[7]=1.0;add_inequality(model,negate_row(group),-.15);add_inequality(model,group,.35);
    return model;
}

inline void fill_constraint_diagnostics(SolveInfo& info,const Row& w,const Row& previous){
    auto c=get_constrain(w,previous);info.sum_weight_error=std::abs(c[0]);double turnover=0.0;for(int i=0;i<N;++i)turnover+=std::abs(w[i]-previous[i]);info.turnover_error=std::max(0.0,turnover-.1);info.group_violation=0.0;for(int i=10;i<M;++i)info.group_violation=std::max(info.group_violation,std::max(0.0,-c[i]));info.max_violation=violation(w,previous);
}

inline SolveInfo solve_convex_weights(const Statistics& statistics_value,const Row& previous,const Options& options){
    SolveInfo info;info.weight=previous;info.solver_kind=options.risk_averse?SolverKind::QP:SolverKind::LP;
    WeightConvexModel model=make_weight_convex_model(statistics_value,previous,options);ActiveSetQpSolver solver;
    auto solved=solver.solve(model.h,model.f,model.a,model.b,model.equality,model.equality_rhs,model.initial,model.initial_active,2000,1e-10);
    info.iterations=solved.iterations;info.objective=solved.objective;info.primal_residual=solved.primal_residual;info.dual_residual=solved.dual_residual;info.kkt_residual=solved.kkt_residual;info.message=solved.message;info.status=solved.success?0:9;
    if(solved.x.size()>=N)for(int i=0;i<N;++i)info.weight[i]=solved.x[i];fill_constraint_diagnostics(info,info.weight,previous);info.success=solved.success && info.max_violation<=1e-8 && std::all_of(info.weight.begin(),info.weight.end(),[](double v){return std::isfinite(v);});
    if(!info.success){info.fallback=true;info.message="convex solver validation failed: "+info.message;}
    return info;
}

struct DiversificationModel {
    std::vector<std::vector<double>> h,a,equality;
    std::vector<double> f,b,equality_rhs,initial;
    std::vector<int> initial_active;
};
inline int add_div_inequality(DiversificationModel& model,const std::vector<double>& row,double rhs){model.a.push_back(row);model.b.push_back(rhs);return static_cast<int>(model.a.size()-1);}
inline DiversificationModel make_diversification_model(const Statistics& s,const Row& previous){
    constexpr int S=N,T=N+1,n=2*N+1;DiversificationModel model;model.h.assign(n,std::vector<double>(n,0.0));model.f.assign(n,0.0);model.initial.assign(n,0.0);Row sigma=s.stddev;double scale=0.0;for(int i=0;i<N;++i)scale+=sigma[i]*previous[i];
    if(!(scale>1e-14))return model;for(int i=0;i<N;++i)for(int j=0;j<N;++j)model.h[i][j]=2.0*s.cov[i][j];for(int i=0;i<n;++i)model.h[i][i]+=1e-12;
    for(int i=0;i<N;++i)model.initial[i]=previous[i]/scale;model.initial[S]=1.0/scale;for(int i=0;i<N;++i)model.initial[T+i]=0.0;
    std::vector<double> eq1(n,0.0),eq2(n,0.0);for(int i=0;i<N;++i)eq1[i]=sigma[i];for(int i=0;i<N;++i)eq2[i]=1.0;eq2[S]=-1.0;model.equality={eq1,eq2};model.equality_rhs={1.0,0.0};
    for(int i=0;i<N;++i){std::vector<double> row(n,0.0);row[i]=-1.0;add_div_inequality(model,row,0.0);row.assign(n,0.0);row[T+i]=-1.0;model.initial_active.push_back(add_div_inequality(model,row,0.0));}
    std::vector<double> group(n,0.0);for(int i=0;i<4;++i)group[i]=1.0;group[S]=-.35;add_div_inequality(model,group,0.0);group.assign(n,0.0);for(int i=0;i<4;++i)group[i]=-1.0;group[S]=.15;add_div_inequality(model,group,0.0);
    group.assign(n,0.0);group[4]=1.0;group[S]=-.35;add_div_inequality(model,group,0.0);group.assign(n,0.0);group[4]=-1.0;group[S]=.15;add_div_inequality(model,group,0.0);
    group.assign(n,0.0);group[5]=1.0;group[S]=-.35;add_div_inequality(model,group,0.0);group.assign(n,0.0);group[5]=-1.0;group[S]=.15;add_div_inequality(model,group,0.0);
    group.assign(n,0.0);group[6]=group[7]=1.0;group[S]=-.35;add_div_inequality(model,group,0.0);group.assign(n,0.0);group[6]=group[7]=-1.0;group[S]=.15;add_div_inequality(model,group,0.0);
    for(int i=0;i<N;++i){std::vector<double> row(n,0.0);row[i]=1.0;row[S]=-previous[i];row[T+i]=-1.0;add_div_inequality(model,row,0.0);row.assign(n,0.0);row[i]=-1.0;row[S]=previous[i];row[T+i]=-1.0;add_div_inequality(model,row,0.0);}
    std::vector<double> turn(n,0.0);for(int i=0;i<N;++i)turn[T+i]=1.0;turn[S]=-.1;add_div_inequality(model,turn,0.0);
    return model;
}
inline SolveInfo solve_convex_diversification(const Statistics& s,const Row& previous){
    SolveInfo info;info.weight=previous;info.solver_kind=SolverKind::QP;auto model=make_diversification_model(s,previous);if(model.initial.empty() || model.equality.empty()){info.fallback=true;info.message="zero volatility prevents diversification normalization";return info;}
    ActiveSetQpSolver solver;auto solved=solver.solve(model.h,model.f,model.a,model.b,model.equality,model.equality_rhs,model.initial,model.initial_active,3000,1e-10);info.iterations=solved.iterations;info.objective=solved.objective;info.primal_residual=solved.primal_residual;info.dual_residual=solved.dual_residual;info.kkt_residual=solved.kkt_residual;info.message=solved.message;info.status=solved.success?0:9;
    if(solved.x.size()>=2*N+1 && solved.x[N]>1e-14)for(int i=0;i<N;++i)info.weight[i]=solved.x[i]/solved.x[N];fill_constraint_diagnostics(info,info.weight,previous);info.success=solved.success && info.max_violation<=1e-8 && std::all_of(info.weight.begin(),info.weight.end(),[](double v){return std::isfinite(v);});if(!info.success)info.fallback=true;return info;
}
class SlsqpSolver {
    static constexpr int n1=N+1,mineq=M-1+2*n1;
    static constexpr int length=(3*n1+M)*(n1+1)+n1*(mineq+2)+2*mineq+(n1+mineq)*(n1-1)+2+n1*N/2+2*M+3*N+4*n1+1;
    std::array<double,length+32> work_{};std::array<int,mineq> iw_{};
    std::array<double,M*(N+1)> jac_{};std::array<double,N+1> gradient_{};
    Row lower_{},upper_{};
    static double objective(const Row& w,const Statistics& s,const Options& o,Row* grad){
        // Fused Sigma*w reused by objective and its analytic gradient.
        Row cw{};for(int a=0;a<N;++a)for(int b=0;b<N;++b)cw[a]+=s.cov[a][b]*w[b];
        double q=0;for(int j=0;j<N;++j)q+=w[j]*cw[j];
        if(o.risk_averse){double f=0;for(int j=0;j<N;++j)f-=w[j]*s.mean[j];if(grad)for(int j=0;j<N;++j)(*grad)[j]=-s.mean[j]+*o.risk_averse*cw[j];return f+.5**o.risk_averse*q;}
        const Row& v=o.ewm?s.variance:s.stddev;double numerator=0;for(int j=0;j<N;++j)numerator+=w[j]*v[j];
        double root=std::sqrt(std::max(q,1e-30));if(grad)for(int j=0;j<N;++j)(*grad)[j]=-v[j]/root+numerator*cw[j]/(root*root*root);return -numerator/root;
    }
    SolveInfo attempt(const Statistics& s,const Row& prev,const Options& o){
        SolveInfo out;out.weight=prev;work_.fill(0);iw_.fill(0);jac_.fill(0);gradient_.fill(0);
        Constraints c;double f=0;int m=M,meq=1,la=M,n=N,lw=length+32,li=mineq,iter=std::max(1,o.max_iterations-1),mode=0;double acc=o.ftol;slsqpb_state state{};
        auto evaluate=[&](bool grad){Row g;f=objective(out.weight,s,o,grad && o.solver==SolverMode::Analytic?&g:nullptr);++out.evaluations;c=get_constrain(out.weight,prev);
            if(grad){constexpr double epsilon=1.490116119384765625e-8;
                for(int j=0;j<N;++j){Row x=out.weight;x[j]+=epsilon;double h=x[j]-out.weight[j];auto cp=get_constrain(x,prev);
                    if(o.solver==SolverMode::Compatible){gradient_[j]=(objective(x,s,o,nullptr)-f)/h;++out.evaluations;}else gradient_[j]=g[j];
                    // The L1 kink uses the same forward secant as scipy approx_derivative.
                    for(int i=0;i<M;++i)jac_[i+j*M]=(cp[i]-c[i])/h;
                }
                gradient_[N]=0;
            }
        };
        evaluate(true);int calls=0;
        for(;calls<100000;++calls){slsqp(&m,&meq,&la,&n,out.weight.data(),lower_.data(),upper_.data(),&f,c.data(),gradient_.data(),jac_.data(),&acc,&iter,&mode,work_.data(),&lw,iw_.data(),&li,&state);
            if(mode==1)evaluate(false);else if(mode==-1 || mode==-2)evaluate(true);else break;
        }
        out.status=calls>=100000?9:mode;out.iterations=iter;out.objective=objective(out.weight,s,o,nullptr);fill_constraint_diagnostics(out,out.weight,prev);out.success=(out.status==0 && out.max_violation<=std::max(1e-6,10*o.ftol) && std::isfinite(out.objective));out.message=out.success?"SLSQP converged":"SLSQP did not pass validation";return out;
    }
public:
    SlsqpSolver(){lower_.fill(-std::numeric_limits<double>::infinity());upper_.fill(std::numeric_limits<double>::infinity());}
    SolveInfo solve(const Statistics& s,const Row& prev,const Options& o){
        if(!o.risk_averse && *std::max_element(s.variance.begin(),s.variance.end())<=0){SolveInfo r;r.weight=prev;r.status=5;r.fallback=true;return r;}
        auto r=attempt(s,prev,o);
        if(o.solver==SolverMode::Analytic && (r.status!=0 || r.max_violation>1e-6)){Options compatible=o;compatible.solver=SolverMode::Compatible;auto retry=attempt(s,prev,compatible);retry.evaluations+=r.evaluations;retry.retry=true;r=retry;}
        if(r.max_violation>std::max(1e-6,10*o.ftol) || !std::isfinite(r.objective)){r.weight=prev;r.fallback=true;r.success=false;r.max_violation=violation(prev,prev);r.objective=objective(prev,s,o,nullptr);r.message="SLSQP fallback to previous valid weight";}
        return r;
    }
};
struct PortfolioResult {Series weight;std::vector<double> weighted_return,pnl;std::vector<SolveInfo> solves;int fallback_count=0,retry_count=0,solver_failure_count=0;};
class MVO {
    const Series& df_return_;Options options_;SlsqpSolver solver_;
    void validate() const {
        if(options_.window<2 || options_.keep<1 || options_.keep>=options_.window || options_.yr_dates<=0 || options_.max_iterations<=0 || options_.ftol<=0 || !std::isfinite(options_.ftol) || !std::isfinite(options_.sigma) || options_.sigma<0)throw std::invalid_argument("invalid MVO options");
        if(options_.risk_averse && (!std::isfinite(*options_.risk_averse) || *options_.risk_averse<0))throw std::invalid_argument("risk aversion must be nonnegative");
        if(violation(options_.init_weight,options_.init_weight)>1e-8)throw std::invalid_argument("initial weights must satisfy all constraints");
        for(auto& r:df_return_)for(double v:r)if(!std::isfinite(v))throw std::invalid_argument("MVO return data must be finite");
    }
public:
    MVO(const Series& df_return,const Options& options):df_return_(df_return),options_(options){validate();}
    MVO(const Series& df_return,int window=120,int keep=1,
        const Row& init_weight=initial_weights,
        std::optional<double> risk_averse=std::nullopt,
        std::optional<bool> ewm=std::nullopt)
        : df_return_(df_return), options_{} {
        options_.window=window; options_.keep=keep; options_.init_weight=init_weight;
        options_.risk_averse=risk_averse; if(ewm) options_.ewm=*ewm; validate();
    }
    Series norm_preprocess(const Series& x,int window=120,double sigma=3) const {return mvo::norm_preprocess(x,window,sigma);}
    Constraints get_constrain(const Row& prev,const Row& w) const {return mvo::get_constrain(w,prev);}
    Row get_daily_div_weight(const Series& x,const Row& prev){auto o=options_;o.risk_averse.reset();return solver_.solve(statistics(x,0,x.size(),o.window,o.ewm),prev,o).weight;}
    Row get_daily_mvo_weight(const Series& x,const Row& prev){if(!options_.risk_averse)throw std::invalid_argument("risk_averse required");return solver_.solve(statistics(x,0,x.size(),options_.window,options_.ewm),prev,options_).weight;}
    PortfolioResult run(const PreparedData* cached=nullptr){
        auto own=cached?PreparedData{}:prepare(df_return_,options_.window,options_.keep,options_.sigma,options_.accuracy);const auto& p=cached?*cached:own;
        PortfolioResult r;r.weight.resize(df_return_.size());r.weighted_return.resize(df_return_.size());r.pnl.resize(df_return_.size());r.solves.reserve(p.schedule.size());
        Row prev=options_.init_weight;size_t next=0;double sum=0;
        for(size_t i=0;i<df_return_.size();++i){
            if(next<p.schedule.size() && p.schedule[next].day==i){
                auto& at=p.schedule[next++];const Statistics& moments=options_.ewm?at.ewm:at.equal;SolveInfo solved;
                if(options_.accuracy==AccuracyMode::HighAccuracy){
                    solved=options_.risk_averse?solve_convex_weights(moments,prev,options_):solve_convex_diversification(moments,prev);
                    if(!solved.success){const std::string primary_message=solved.message+" primal="+std::to_string(solved.primal_residual)+" dual="+std::to_string(solved.dual_residual)+" kkt="+std::to_string(solved.kkt_residual);auto fallback=solver_.solve(moments,prev,options_);fallback.retry=true;fallback.fallback=true;fallback.message="convex primary failed ("+primary_message+"); SLSQP fallback: "+fallback.message;solved=fallback;}
                } else solved=solver_.solve(moments,prev,options_);
                if(solved.fallback)++r.fallback_count;if(solved.retry)++r.retry_count;if(!solved.success)++r.solver_failure_count;r.solves.push_back(solved);prev=solved.weight;
            }
            r.weight[i]=prev;double v=0;for(int j=0;j<N;++j)v+=prev[j]*df_return_[i][j];r.weighted_return[i]=v;sum+=v;r.pnl[i]=sum+1;
        }
        return r;
    }
    Series get_weight(){return run().weight;}
};
} // namespace mvo
