import sys
import numpy, scipy, pandas
print(sys.version)
print(numpy.__version__, scipy.__version__, pandas.__version__)
import importlib.util
print('xlrd', importlib.util.find_spec('xlrd'))
