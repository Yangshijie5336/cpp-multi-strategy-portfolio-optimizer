import warnings
warnings.filterwarnings('ignore')
import pandas as pd
import numpy as np
from scipy.optimize import minimize



##计算各种指标
class EvaluatePNL:
    def __init__(self, pnl=None, yr_dates=250):
        self.pnl = pnl
        self.yr_dates = yr_dates #250 or 365
    
    def get_return(self, df_pnl): ##年化收益
        tmp_return = df_pnl.diff(1).fillna(0)
        tmp_return = tmp_return[tmp_return != 0]
        return tmp_return.mean() * self.yr_dates 

    def get_std(self, df_pnl): ## 年化波动率
        tmp_return = df_pnl.diff(1).fillna(0)
        tmp_return = tmp_return[tmp_return != 0]
        return tmp_return.std() * self.yr_dates ** .5

    def get_sharp(self, df_pnl):        
        std = self.get_std(df_pnl)
        avg = self.get_return(df_pnl)
        return avg / std

    def get_max_drawdown(self, df_pnl, get_date=False):
        n_days = len(df_pnl)
        idx0 = np.triu_indices(n_days, 1)[0]
        idx1 = np.triu_indices(n_days, 1)[1]
        diff = df_pnl.values[idx0] - df_pnl.values[idx1]
        min_idx = np.argmax(diff)
        left = idx0[min_idx]
        right = idx1[min_idx]
        if get_date:
            date = (df_pnl.index[left], df_pnl.index[right])
            return diff[min_idx], date
        else:
            return diff[min_idx]

    def get_win_percent(self, df_pnl):
        daily_return = df_pnl.diff(1).fillna(0)
        return (daily_return > 0).sum() / (1. * (daily_return != 0).sum())

    def get_calmar(self, df_pnl):
        max_drawdown = self.get_max_drawdown(df_pnl)
        abs_return = df_pnl.iloc[-1] - df_pnl.iloc[0]
        yrs = (df_pnl.index[-1] - df_pnl.index[0]).days / 365.
        return abs_return / (yrs * max_drawdown)

    @property
    def test_pipline(self):
        rt = self.get_return(self.pnl)
        sharp = self.get_sharp(self.pnl)
        std = self.get_std(self.pnl)
        max_drawdown, date = self.get_max_drawdown(self.pnl, get_date=True)
        win_pct = self.get_win_percent(self.pnl) 
        calmar = self.get_calmar(self.pnl)
        print('{:.2f} & {:.2f} & {:.3f} & {:.3f} & {:.2f} & {:.2f} & {:} & {:}'.format(
            rt * 100, std * 100, sharp, calmar, win_pct * 100, max_drawdown * 100, date[0].strftime('%Y%m%d'), date[1].strftime('%Y%m%d')))


def norm_preprocess(df_return, window=120, n_sigma=3):
    '''
    3sigma 数据极值预处理
    '''
    df_new_return = df_return.copy()
    for i in range(len(df_return)):
        if i >= window:
            mean = df_new_return.iloc[(i-window):i].mean()
            std = df_new_return.iloc[(i-window):i].std()
            mask1 = df_new_return.iloc[i] > (mean + n_sigma * std)
            mask2 = df_new_return.iloc[i] < (mean - n_sigma * std)
            df_new_return.iloc[i, mask1] = (mean + n_sigma * std)[mask1]
            df_new_return.iloc[i, mask2] = (mean - n_sigma * std)[mask2]    
    return df_new_return

class MVO(object):

    def __init__(self, df_return, window=120, keep=1, init_weight=None, 
                 risk_averse=None, ewm=None):
        self.df_return = df_return
        self.window = window # 回看时间
        self.keep = keep # 保持时间             
        
        self.init_weight = init_weight #初始设置的权重
        
        self.risk_averse = risk_averse
        self.ewm = ewm

    def norm_preprocess(self, df_return, window=120, n_sigma=3):
        '''
        3sigma 数据极值预处理
        '''
        df_new_return = df_return.copy()
        for i in range(len(df_return)):
            if i >= window:
                mean = df_new_return.iloc[(i-window):i].mean()
                std = df_new_return.iloc[(i-window):i].std()
                mask1 = df_new_return.iloc[i] > (mean + n_sigma * std)
                mask2 = df_new_return.iloc[i] < (mean - n_sigma * std)
                df_new_return.iloc[i, mask1] = (mean + n_sigma * std)[mask1]
                df_new_return.iloc[i, mask2] = (mean - n_sigma * std)[mask2]    
        return df_new_return

    def get_constrain(self, prev_weight):
        '''
        权重的约束条件
        '''
        cons = (
            {'type': 'ineq', 'fun': lambda w: w},             
            # {'type': 'ineq', 'fun': lambda w: 0.4 - w},
            {'type': 'eq', 'fun': lambda w: np.sum(w) - 1.},
            {'type': 'ineq', 'fun': lambda w: 0.1 - np.sum(np.absolute(w - prev_weight))},
            {'type': 'ineq', 'fun': lambda w: w[0] + w[1] + w[2] + w[3] - 0.15}, 
            {'type': 'ineq', 'fun': lambda w: 0.35 - (w[0] + w[1] + w[2] + w[3])}, 
            {'type': 'ineq', 'fun': lambda w: w[4] - 0.15}, 
            {'type': 'ineq', 'fun': lambda w: 0.35 - w[4]}, 
            {'type': 'ineq', 'fun': lambda w: w[5] - 0.15}, 
            {'type': 'ineq', 'fun': lambda w: 0.35 - w[5]}, 
            {'type': 'ineq', 'fun': lambda w: w[6] + w[7] - 0.15}, 
            {'type': 'ineq', 'fun': lambda w: 0.35 - (w[6] + w[7])}, 
            )
        return cons
            
    def get_daily_div_weight(self, w_return, prev_weight):   
        '''
        根据过去一段收益率序列给出子策略权重，利用最大分散 
        '''                   
        if self.ewm is None:
            std = w_return.std().values
            cov = w_return.cov().values
        else: ###使用指数加权平均进行协方差标准差估计
            cov = w_return.ewm(span=self.window, adjust=False).cov().values[-len(w_return.iloc[0]):]
            std = np.diagonal(cov) 

        obj_func = lambda w: -np.dot(w, std) / np.sqrt(np.dot(np.dot(w, cov), w)) ## -1 * 分散度
        ## 权重限制
        cons = self.get_constrain(prev_weight=prev_weight)
        options = {'maxiter': 1000}
        res = minimize(obj_func, prev_weight, method='SLSQP', constraints=cons, options=options)
        return res.x    
    
    def get_daily_mvo_weight(self, w_return, prev_weight):        
        '''
        均值-方差优化
        '''
        if self.ewm is None:
            rt = w_return.mean().values
            cov = w_return.cov().values
        else:
            rt = w_return.ewm(span=self.window, adjust=False).mean().iloc[-1].values
            cov = w_return.ewm(span=self.window, adjust=False).cov().values[-len(rt):]

        obj_func = lambda w: -np.sum(w*rt) + 0.5 * self.risk_averse * np.dot(np.dot(w, cov), w) ## -1 * U(w)
        ## 权重限制
        cons = self.get_constrain(prev_weight=prev_weight)
        options = {'maxiter': 1000}
        res = minimize(obj_func, prev_weight, method='SLSQP', constraints=cons, options=options)
        return res.x      
    
    def get_weight(self):
        '''
        根据目标优化方法计算子策略权重序列
        '''
        df_weight = pd.DataFrame(1., index=self.df_return.index, columns=self.df_return.columns) #初始化子策略权重
        df_weight = df_weight * self.init_weight
        
        processed_return = self.norm_preprocess(self.df_return, window=self.window, n_sigma=3) #收益率序列预处理
            
        for i in range(len(self.df_return)):
            if i >= self.window: #当日期大于回看区间时，开始对权重进行调整，否则按照初始权重处理
                if (i - self.window) % self.keep == 0: # 开始调整日期                                        
                    w_return = processed_return.iloc[(i-self.window):i] #回看时间段的子策略收益率序列                    
                    w_return = w_return.rolling(self.keep).sum().dropna()
    
                    if self.risk_averse is not None:
                        daily_weight = self.get_daily_mvo_weight(w_return, df_weight.iloc[i-1])
                    else:                        
                        daily_weight = self.get_daily_div_weight(w_return, df_weight.iloc[i-1]) #计算得到子策略该日的权重                        
                        
                    df_weight.iloc[i] = daily_weight 
                else:
                    df_weight.iloc[i] = df_weight.iloc[i-1] # 否则延续前一天权重        
            
        return df_weight
        
    @property
    def run(self):
        self.weight = self.get_weight() #计算得到子策略的权重序列
        self.weighted_return = (self.weight * self.df_return).sum(axis=1) #计算得到最大分散调整之后子策略的权重序列     

if __name__ == "__main__":
    ############################ 读取PNL数据 ############################
    df = pd.read_excel('data.xls')
    df = df[['Unnamed: 0', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h']]
    df = df.rename(mapper={'Unnamed: 0': 'Date'}, axis=1)
    df.index = pd.to_datetime(df.Date)
    df = df.drop('Date', axis=1)    

    df_return = df.diff(1).fillna(0) ##日收益率

    init_weight = np.array([8.9, 10.25, 6.35, 6.45, 25.72, 16.55, 12.72, 13.06]) / 100 ##默认权重    
    # init_weight = np.array([1./len(df.T) for _ in range(len(df.T))]) ##默认权重    

    ############################ 对比不同权重调整方法 ############################
    window = 60 #回看时间
    keep = 15 #保持时间
    risk_averses = {'最大分散': None, '最大目标收益率': 0, '风险厌恶-20': 20}
    ewms = {'等权重加权': None, '指数加权': True}
    # ewms = {'等权重加权': None}
    # 初始化对象
    for risk in risk_averses:
        for ewm in ewms:
            print(f'回看{window}日，保持{keep}日...')
            print(f'{risk}, {ewm}...')
            portfolio = MVO(df_return,
                            window=window, 
                            keep=keep,      
                            init_weight=init_weight,
                            risk_averse=risk_averses[risk],
                            ewm=ewms[ewm])
            portfolio.run #进行权重优化
            print('技术指标：')
            EvaluatePNL(portfolio.weighted_return.cumsum() + 1).test_pipline
            print('')

    print('初始权重，基准组合技术指标：')
    EvaluatePNL((portfolio.df_return * init_weight).sum(axis=1).cumsum() + 1).test_pipline
        