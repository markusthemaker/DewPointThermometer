% OUTDOOR 
% Reads public Adafruit IO feeds for Temperature & Humidity, computes Dew Point,
% and plots them on dual y-axes. Time is stored in UTC but displayed in CET/CEST.
% We now convert time to numeric (datenum) so we can manage a manual x-axis
% with 6-hour vertical lines, day-boundary labels, and bold lines at -1 and -2 days.

% -------------------------------
% STEP 1: DEFINE PARAMETERS
% -------------------------------
baseURL = 'https://io.adafruit.com/api/v2';

% Your Adafruit IO username (exactly as shown in your Adafruit IO profile)
aioUsername = 'x';  % <-- Replace with your username

% Feed keys for Temperature and Humidity (must be public)
temperatureFeedKey = 'outdoor-temperature';  % <-- Replace with your feed key
humidityFeedKey    = 'outdoor-humidity';     % <-- Replace with your feed key

% Number of data points to retrieve
maxDataPoints = 2500;

% Define the time window in UTC
dateEndUTC   = datetime('now', 'TimeZone', 'UTC'); 
dateStartUTC = dateEndUTC - days(3);  % 3 days ago in UTC

%% -------------------------------
% STEP 2: FETCH DATA (PUBLIC FEEDS)
% -------------------------------
temperatureDataUTC = fetchAdafruitIOData( ...
    baseURL, aioUsername, temperatureFeedKey, dateStartUTC, dateEndUTC, maxDataPoints);

humidityDataUTC = fetchAdafruitIOData( ...
    baseURL, aioUsername, humidityFeedKey, dateStartUTC, dateEndUTC, maxDataPoints);

%% -------------------------------
% STEP 3: VALIDATE & SYNCHRONIZE DATA
% -------------------------------
if isempty(temperatureDataUTC) || height(temperatureDataUTC) == 0
    error('No Temperature data retrieved. Check if your Temperature feed is public and has data.');
end

if isempty(humidityDataUTC) || height(humidityDataUTC) == 0
    error('No Humidity data retrieved. Check if your Humidity feed is public and has data.');
end

% Merge into one timetable (outer join, linear interpolation)
commonDataUTC = synchronize(temperatureDataUTC, humidityDataUTC, 'union', 'linear');

% Rename columns for clarity
commonDataUTC.Properties.VariableNames = {'Temperature', 'Humidity'};

%% -------------------------------
% STEP 4: COMPUTE DEW POINT
% -------------------------------
% Using Magnus-Tetens formula
tempC       = commonDataUTC.Temperature;
relHumidity = commonDataUTC.Humidity;

a = 17.27;
b = 237.7;  % in °C

gamma = (a .* tempC) ./ (b + tempC) + log(relHumidity / 100);
dewPointC = (b * gamma) ./ (a - gamma);

% Add DewPoint as a new column in the timetable
commonDataUTC.DewPoint = dewPointC;

%% -------------------------------
% STEP 5: FILTER TIME (OPTIONAL)
% -------------------------------
commonDataUTC = commonDataUTC(commonDataUTC.Time >= dateStartUTC & ...
                              commonDataUTC.Time <= dateEndUTC, :);

%% -------------------------------
% STEP 6: CONVERT TIME TO LOCAL (CET/CEST)
% -------------------------------
commonDataUTC.Time.TimeZone = 'Europe/Berlin';

%% -------------------------------
% STEP 7: CREATE THE PLOT (USING NUMERIC X-AXIS)
% -------------------------------
timeLocal   = commonDataUTC.Time;         % datetime
timeNum     = datenum(timeLocal);         % numeric days since year 0000

temperature = commonDataUTC.Temperature;
humidity    = commonDataUTC.Humidity;
dewPoint    = commonDataUTC.DewPoint;

figure('Position', [100, 100, 1000, 600], 'Renderer', 'opengl');

% --- LEFT Y-AXIS: Temperature & Dew Point ---
yyaxis left
plot(timeNum, temperature, '-r', 'LineWidth', 2, ...
    'DisplayName','Temperature (°C)', 'Color',[1, 0, 0, 0.7]);
hold on
plot(timeNum, dewPoint, '--r', 'LineWidth', 2, ...
    'DisplayName','Dew Point (°C)', 'Color',[1, 0, 0, 0.7]);

ylabel('Temperature / Dew Point (°C)');
ylim([min([temperature; dewPoint]) - 2, max([temperature; dewPoint]) + 2]);
ax = gca; 
ax.YColor = 'r';

% --- RIGHT Y-AXIS: Humidity ---
yyaxis right
plot(timeNum, humidity, '-b', 'LineWidth', 2, ...
    'DisplayName','Humidity (%)', 'Color',[0, 0, 1, 0.7]);
ylabel('Humidity (%)');

humMin = min(humidity, [], 'omitnan');
humMax = max(humidity, [], 'omitnan');
defaultHumMin = 0;
defaultHumMax = 100;

newYmin = min(defaultHumMin, humMin);
newYmax = max(defaultHumMax, humMax);
ylim([newYmin, newYmax]);
ax.YColor = 'b';

title('Outdoor Station');
grid on;

set(ax, 'FontSize', 12, 'LineWidth', 1.5);

%% --- DEFINE X-AXIS: 3 DAYS, 6-HOUR TICKS, DAY BOUNDARIES ---
% 1) Numeric range: last data => endLocalNum, 3 days earlier => startLocalNum
endLocalNum   = max(timeNum);
startLocalNum = endLocalNum - 3;  % 3 days is "3.0" in datenum

xlim([startLocalNum, endLocalNum]);

% 2) 6-hour increments => 6/24 = 0.25 days
allTicks_6h_num = (startLocalNum : 0.25 : endLocalNum);

% 3) Day boundaries => +1.0 for each day
majorTicks_1d_num = [startLocalNum, startLocalNum+1, ...
                     startLocalNum+2, endLocalNum];

% 4) Place a tick at every 6 hours
ax.XTick = allTicks_6h_num;

% Assign default labels for every tick
ax.XTickLabel = cellstr( datestr(allTicks_6h_num, 'dd-mmm-yy HH:MM') );

% 5) Re-label day boundaries (optional, already labeled if you want)
%    If you wish to highlight them:
[~, locMaj] = ismembertol(majorTicks_1d_num, allTicks_6h_num, 1e-8);
for i = 1:numel(locMaj)
    if locMaj(i) ~= 0
        ax.XTickLabel{locMaj(i)} = datestr(allTicks_6h_num(locMaj(i)), 'dd-mmm-yy HH:MM');
    end
end

% 6) Vertical grid lines at each 6-hour tick
grid on;

% --- BOLD LINES at current time minus 1 day and minus 2 days ---
line1 = endLocalNum - 1;
line2 = endLocalNum - 2;

% xline is R2018b+; otherwise do plot([line1 line1], ylim, 'k', 'LineWidth', 1)
xline(line1, 'k', 'LineWidth', 1);
xline(line2, 'k', 'LineWidth', 1);

%% -------------------------------
% STEP 8: OPTIONAL - SAVE THE PLOT
% -------------------------------
% saveas(gcf, 'Mstein_Outdoor_Plot.png');

% -------------------------------
% LOCAL FUNCTION: fetchAdafruitIOData
% -------------------------------
function data = fetchAdafruitIOData(baseURL, aioUsername, feedKey, ...
                                    dateStartUTC, dateEndUTC, maxDataPoints)
    % Build base URL
    url = sprintf('%s/%s/feeds/%s/data', baseURL, aioUsername, feedKey);

    % Convert dateStartUTC, dateEndUTC to ISO-8601 strings
    startStr = datestr(dateStartUTC, 'yyyy-mm-ddTHH:MM:SSZ');
    endStr   = datestr(dateEndUTC,   'yyyy-mm-ddTHH:MM:SSZ');

    % Prepare query parameters
    % - limit: up to your maxDataPoints
    % - start_time: earliest time in UTC
    % - end_time: latest time in UTC
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

    % If empty, return an empty timetable
    if isempty(rawData)
        data = timetable();
        return
    end

    % Convert timestamps from "YYYY-MM-DDTHH:mm:ssZ" to datetime (UTC)
    timestampsUTC = datetime({rawData.created_at}, ...
        'InputFormat','yyyy-MM-dd''T''HH:mm:ss''Z''', ...
        'TimeZone','UTC');

    % Convert 'value' to double
    values = str2double({rawData.value});

    % Create timetable
    data = timetable(timestampsUTC', values', 'VariableNames', {'Value'});

    % (Optional) Filter again (should already be correct, but just in case)
    data = data(data.Time >= dateStartUTC & data.Time <= dateEndUTC, :);

    % Sort by ascending time
    data = sortrows(data);
end
