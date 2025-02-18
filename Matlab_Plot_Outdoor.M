% Reads public Adafruit IO feeds or Thingspeak channel data for Temperature &
% Humidity, computes Dew Point, and plots them on dual y-axes. 
% We convert time to numeric (datenum) so we can safely manage 
% a manual x-axis with 6-hour vertical lines and day-boundary labels.

% -------------------------------
% STEP 1: DEFINE PARAMETERS
% -------------------------------

% Choose data source: 'adafruit' or 'thingspeak'
dataSource = 'thingspeak';  % Change to 'thingspeak' to use Thingspeak data

if strcmpi(dataSource, 'adafruit')
    baseURL = 'https://io.adafruit.com/api/v2';
    aioUsername = 'x';  % <-- Replace with your username
    temperatureFeedKey = 'outdoor-temperature';  % <-- Replace with your feed key
    humidityFeedKey    = 'outdoor-humidity';     % <-- Replace with your feed key
else  % thingspeak
    thingspeakChannelID = 'x';    % <-- Replace with your Thingspeak Channel ID
    thingspeakReadAPIKey = 'x';   % <-- Replace with your Thingspeak Read API Key
    temperatureField = 4;  % Field number for Temperature
    humidityField    = 5;  % Field number for Humidity
end

maxDataPoints = 3000;

% 3 days up to "now" in UTC
dateEndUTC   = datetime('now', 'TimeZone', 'UTC'); 
dateStartUTC = dateEndUTC - days(3);

%% -------------------------------
% STEP 2: FETCH DATA
% -------------------------------
if strcmpi(dataSource, 'adafruit')
    temperatureDataUTC = fetchAdafruitIOData( ...
        baseURL, aioUsername, temperatureFeedKey, dateStartUTC, dateEndUTC, maxDataPoints);

    humidityDataUTC = fetchAdafruitIOData( ...
        baseURL, aioUsername, humidityFeedKey, dateStartUTC, dateEndUTC, maxDataPoints);
elseif strcmpi(dataSource, 'thingspeak')
    temperatureDataUTC = fetchThingspeakData( ...
        thingspeakChannelID, temperatureField, thingspeakReadAPIKey, dateStartUTC, dateEndUTC, maxDataPoints);
    humidityDataUTC = fetchThingspeakData( ...
        thingspeakChannelID, humidityField, thingspeakReadAPIKey, dateStartUTC, dateEndUTC, maxDataPoints);
else
    error('Unsupported dataSource specified.');
end

%% -------------------------------
% STEP 3: VALIDATE & SYNCHRONIZE DATA
% -------------------------------
if isempty(temperatureDataUTC) || height(temperatureDataUTC) == 0
    error('No Temperature data retrieved. Check if your Temperature feed is public and has data.');
end

if isempty(humidityDataUTC) || height(humidityDataUTC) == 0
    error('No Humidity data retrieved. Check if your Humidity feed is public and has data.');
end

commonDataUTC = synchronize(temperatureDataUTC, humidityDataUTC, 'union', 'linear');
commonDataUTC.Properties.VariableNames = {'Temperature', 'Humidity'};

%% -------------------------------
% STEP 4: COMPUTE DEW POINT
% -------------------------------
tempC       = commonDataUTC.Temperature;
relHumidity = commonDataUTC.Humidity;

a = 17.27; 
b = 237.7;  % in °C
% Use max(relHumidity, eps) to avoid taking log of zero or negative values
gamma = (a.*tempC)./(b+tempC) + log(max(relHumidity, eps)/100);
dewPointC = (b*gamma)./(a-gamma);

commonDataUTC.DewPoint = dewPointC;

%% -------------------------------
% STEP 5: OPTIONAL FILTER
% -------------------------------
commonDataUTC = commonDataUTC(commonDataUTC.Time >= dateStartUTC & ...
                              commonDataUTC.Time <= dateEndUTC, :);

%% -------------------------------
% STEP 6: TIME TO LOCAL
% -------------------------------
commonDataUTC.Time.TimeZone = 'Europe/Berlin';

%% -------------------------------
% STEP 7: PLOT (NUMERIC AXIS)
% -------------------------------
% 1) Convert time (datetime) -> numeric (datenum):
timeLocal    = commonDataUTC.Time;   % datetime
timeNum      = datenum(timeLocal);   % numeric

temperature  = commonDataUTC.Temperature;
humidity     = commonDataUTC.Humidity;
dewPoint     = commonDataUTC.DewPoint;

figure('Position', [100, 100, 1000, 600], 'Renderer', 'opengl');

% -- LEFT Y-AXIS: Temperature & Dew Point --
yyaxis left
plot(timeNum, temperature, '-r', 'LineWidth', 2, ...
    'DisplayName','Temperature (°C)', 'Color',[1, 0, 0, 0.7]);
hold on
plot(timeNum, dewPoint, '--r', 'LineWidth', 2, ...
    'DisplayName','Dew Point (°C)', 'Color',[1, 0, 0, 0.7]);

ylabel('Temperatur / Taupunkt (°C)');
ylim([min([temperature; dewPoint]) - 2, max([temperature; dewPoint]) + 2]);
ax = gca;
ax.YColor = 'r';

% -- RIGHT Y-AXIS: Humidity --
yyaxis right
plot(timeNum, humidity, '-b', 'LineWidth', 2, ...
    'DisplayName','Humidity (%)', 'Color',[0, 0, 1, 0.7]);
ylabel('Humidity (%)');

humMin = min(humidity, [], 'omitnan');
humMax = max(humidity, [], 'omitnan');
defaultHumMin = 40;
defaultHumMax = 60;
newYmin = min(defaultHumMin, humMin);
newYmax = max(defaultHumMax, humMax);
ylim([newYmin, newYmax]);
ax.YColor = 'b';

title('Outdoor Station');
grid on;

set(ax, 'FontSize', 12, 'LineWidth', 1.5);

%% --- DEFINE X-AXIS (3 DAYS, 6-HOUR LINES, DAY-BOUNDARY LABELS) ---
% 2) Convert our startLocal / endLocal to numeric
%    so we can set x-limits in numeric form
endLocalNum   = max(timeNum);                  % numeric version of last data
startLocalNum = endLocalNum - 3;               % 3 days => 3 in datenum units

xlim([startLocalNum, endLocalNum]);

% 3) 6-hour increments => 6 hours is 6/24 = 0.25 days
allTicks_6h_num = (startLocalNum : 0.25 : endLocalNum);

% 4) Day boundaries => 1 day is 1.0 in datenum
majorTicks_1d_num = [startLocalNum, startLocalNum+1, ...
                     startLocalNum+2, endLocalNum];

% 5) Make the axis show a tick at every 6 hours
ax.XTick = allTicks_6h_num;

% By default, let's blank out all tick labels:
ax.XTickLabel = cellstr( datestr(allTicks_6h_num, 'dd-mmm-yy HH:MM') );

% 6) For the day boundaries, we replace with actual labels:
%    (Use ismembertol to find the index, if necessary, 
%     or just find exact matches if they're integral days.)
[~, locMaj] = ismembertol(majorTicks_1d_num, allTicks_6h_num, 1e-8);

% Use datestr(...) to format those day-boundary ticks
for i = 1:numel(locMaj)
    if locMaj(i) ~= 0
        ax.XTickLabel{locMaj(i)} = datestr(allTicks_6h_num(locMaj(i)), 'dd-mmm-yy HH:MM');
    end
end

% 7) A grid line at each tick => vertical lines every 6 hours
grid on;

% Bold vertical lines at current time minus 1 day and minus 2 days:
line1 = endLocalNum - 1;
line2 = endLocalNum - 2;

xline(line1, 'k', 'LineWidth', 1);
xline(line2, 'k', 'LineWidth', 1);

%% -------------------------------
% STEP 8: OPTIONAL - SAVE
% -------------------------------
% saveas(gcf, 'Mstein_Indoor_Plot.png');

%% -------------------------------
% LOCAL FUNCTIONS
% -------------------------------
function data = fetchAdafruitIOData(baseURL, aioUsername, feedKey, ...
                                    dateStartUTC, dateEndUTC, maxDataPoints)

    url = sprintf('%s/%s/feeds/%s/data', baseURL, aioUsername, feedKey);
    startStr = datestr(dateStartUTC, 'yyyy-mm-ddTHH:MM:SSZ');
    endStr   = datestr(dateEndUTC,   'yyyy-mm-ddTHH:MM:SSZ');

    queryParams = { ...
        'limit',      num2str(maxDataPoints), ...
        'start_time', startStr, ...
        'end_time',   endStr ...
    };

    options = weboptions('ContentType','json','Timeout',30);

    try
        rawData = webread(url, queryParams{:}, options);
    catch ME
        error('Error reading data from Adafruit IO: %s', ME.message);
    end

    if isempty(rawData)
        data = timetable();
        return
    end

    timestampsUTC = datetime({rawData.created_at}, ...
        'InputFormat','yyyy-MM-dd''T''HH:mm:ss''Z''', ...
        'TimeZone','UTC');
    values = str2double({rawData.value});

    data = timetable(timestampsUTC', values', 'VariableNames', {'Value'});
    data = data(data.Time >= dateStartUTC & data.Time <= dateEndUTC, :);
    data = sortrows(data);
end

function data = fetchThingspeakData(channelID, fieldNumber, readAPIKey, ...
                                      dateStartUTC, dateEndUTC, maxDataPoints)
    url = sprintf('https://api.thingspeak.com/channels/%s/fields/%d.json', channelID, fieldNumber);
    startStr = datestr(dateStartUTC, 'yyyy-mm-dd HH:MM:SS');
    endStr   = datestr(dateEndUTC,   'yyyy-mm-dd HH:MM:SS');

    queryParams = { ...
        'api_key', readAPIKey, ...
        'results', num2str(maxDataPoints), ...
        'start', startStr, ...
        'end', endStr ...
    };

    options = weboptions('ContentType','json','Timeout',30);

    try
        result = webread(url, queryParams{:}, options);
    catch ME
        error('Error reading data from Thingspeak: %s', ME.message);
    end

    if ~isfield(result, 'feeds') || isempty(result.feeds)
        data = timetable();
        return
    end

    fieldName = sprintf('field%d', fieldNumber);
    timestampsUTC = datetime({result.feeds.created_at}, ...
        'InputFormat','yyyy-MM-dd''T''HH:mm:ss''Z''', ...
        'TimeZone','UTC');
    values = str2double({result.feeds.(fieldName)});

    data = timetable(timestampsUTC', values', 'VariableNames', {'Value'});
    data = data(data.Time >= dateStartUTC & data.Time <= dateEndUTC, :);
    data = sortrows(data);
end
