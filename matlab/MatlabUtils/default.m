function default(varargin)
% http://dorophone.blogspot.com/2008/03/creating-simple-macros-in-matlab.html
wb = 1;
wa = 1;
vstr = [varargin{:} ';'];
ii = find(vstr=='=');
vstr = [vstr(1:ii(1)) '1;'];
wb = who;
eval(vstr);
wa = who;
v = setdiff(wa,wb);

str = [ '' ...
'if ~exist(''%s''),'...
'  %s;,'...
'end'];
%varargin{:}
str = sprintf(str,v{1},[varargin{:}]);
evalin('caller', str);