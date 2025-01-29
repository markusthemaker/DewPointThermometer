%%TAUPOINT_NUMERICAXIS.M
% Reads public Adafruit IO feeds for Indoor & Outdoor Dew Point,
% linearly interpolates missing data between valid time stamps,
% and plots them on a single y-axis (Taupunkt).
% Also plots a second axis (in black) with the delta (Outdoor-Indoor),
% coloring negative segments in green and positive in red.
% Both y-axes and their labels appear in black.
%
% Now we convert the x-axis to numeric (datenum) to get
% manual 6-hour ticks + day-boundary labels, etc.

% -------------------------------
% STEP 1: DEFINE PARAMETERS
% -------------------------------
baseURL = 'https://io.adafruit.com/api/v2';

% Your Adafruit IO username (exactly as shown in your Adafruit IO profile)
aioUsername = 'x';  % <-- Replace with your username

% Feed keys for Dew Points (must be public)
outdoorFeedKey = 'outdoor-dew-point';  % <-- Replace with your feed key
indoorFeedKey  = 'indoor-dew-point';   % <-- Replace with your feed key

% Number of data points to retrieve
maxDataPoints = 2500;

% Define the time window in UTC
dateEndUTC   = datetime('now', 'TimeZone', 'UTC'); 
dateStartUTC = dateEndUTC - days(3);  % 3 days ago in UTC

%% -------------------------------
% STEP 2: FETCH DATA (PUBLIC FEEDS)
% -------------------------------
outdoorDataUTC = fetchAdafruitIOData( ...
    baseURL, aioUsername, outdoorFeedKey, dateStartUTC, dateEndUTC, maxDataPoints);

indoorDataUTC = fetchAdafruitIOData( ...
    baseURL, aioUsername, indoorFeedKey, dateStartUTC, dateEndUTC, maxDataPoints);

%% -------------------------------
% STEP 3: VALIDATE & SYNCHRONIZE DATA
% -------------------------------
if isempty(outdoorDataUTC) || height(outdoorDataUTC) == 0
    error('No Outdoor Dew Point data retrieved. Check if your feed is public and has data.');
end

if isempty(indoorDataUTC) || height(indoorDataUTC) == 0
    error('No Indoor Dew Point data retrieved. Check if your feed is public and has data.');
end

% We do an 'outer' join and specify 'linear' to interpolate missing data
commonDataUTC = synchronize(outdoorDataUTC, indoorDataUTC, ...
    'union', 'linear');

% Rename columns for clarity
commonDataUTC.Properties.VariableNames = {'OutdoorDP','IndoorDP'};

% By default, 'linear' interpolation can fill values outside each feed's 
% earliest/latest timestamps. We'll handle that below to avoid extrapolation.
tOutdoorStart = min(outdoorDataUTC.Time);
tOutdoorEnd   = max(outdoorDataUTC.Time);
tIndoorStart  = min(indoorDataUTC.Time);
tIndoorEnd    = max(indoorDataUTC.Time);

% Mask times where we don't want to extrapolate for Outdoor
idxOutdoorOutOfRange = (commonDataUTC.Time < tOutdoorStart) ...
    | (commonDataUTC.Time > tOutdoorEnd);
commonDataUTC.OutdoorDP(idxOutdoorOutOfRange) = NaN;

% Mask times where we don't want to extrapolate for Indoor
idxIndoorOutOfRange = (commonDataUTC.Time < tIndoorStart) ...
    | (commonDataUTC.Time > tIndoorEnd);
commonDataUTC.IndoorDP(idxIndoorOutOfRange) = NaN;

%% -------------------------------
% STEP 5: FILTER TO OUR TIME WINDOW (OPTIONAL)
% -------------------------------
commonDataUTC = commonDataUTC(commonDataUTC.Time >= dateStartUTC & ...
                              commonDataUTC.Time <= dateEndUTC, :);

%% -------------------------------
% STEP 6: CONVERT TIME TO LOCAL (CET/CEST)
% -------------------------------
commonDataUTC.Time.TimeZone = 'Europe/Berlin';

%% -------------------------------
% STEP 7: CREATE THE PLOT (NUMERIC X-AXIS)
% -------------------------------
timeLocal = commonDataUTC.Time;
outdoorDP = commonDataUTC.OutdoorDP;
indoorDP  = commonDataUTC.IndoorDP;

% Convert datetime => numeric (datenum)
timeNum = datenum(timeLocal);  % Now we can manually set ticks, etc.

figure('Position', [100, 100, 1000, 600], 'Renderer', 'opengl');
hold on; grid on;

% === LEFT Y-AXIS for Dew Points ===
yyaxis left
axLeft = gca;
axLeft.YColor = 'k';  % black axis color/label

% Plot Outdoor (black dashed)
plot(timeNum, outdoorDP, '--k', 'LineWidth', 2, 'DisplayName','DP Outdoor');

% Plot Indoor (black solid)
plot(timeNum, indoorDP, '-k', 'LineWidth', 2, 'DisplayName','DP Indoor');

ylabel('Dew Point DP (°C)', 'Color', 'k');

dpMin = min([outdoorDP; indoorDP], [], 'omitnan');
dpMax = max([outdoorDP; indoorDP], [], 'omitnan');
if isempty(dpMin) || isnan(dpMin), dpMin = 0; end
if isempty(dpMax) || isnan(dpMax), dpMax = 0; end
ylim([dpMin - 2, dpMax + 2]);

% === RIGHT Y-AXIS for Delta ===
yyaxis right
axRight = gca;
axRight.YColor = 'k';  % black axis color/label

% Compute delta
deltaDP = outdoorDP - indoorDP;
deltaBelow = deltaDP;  % negative => green
deltaBelow(deltaBelow >= 0) = NaN;
deltaAbove = deltaDP;  % positive => red
deltaAbove(deltaAbove <= 0) = NaN;

plot(timeNum, deltaBelow, ':', 'Color',[0,1,0], 'LineWidth',2, ...
     'DisplayName','DP Delta (Outdoor < Indoor)');
plot(timeNum, deltaAbove, ':r', 'LineWidth',2, ...
     'DisplayName','DP Delta (Outdoor > Indoor)');

ylabel('Delta °C (Dew Point Outdoor over/under Indoor)', 'Color', 'k');

deltaMin = min(deltaDP, [], 'omitnan');
deltaMax = max(deltaDP, [], 'omitnan');
if isempty(deltaMin) || isnan(deltaMin), deltaMin = 0; end
if isempty(deltaMax) || isnan(deltaMax), deltaMax = 0; end
ylim([deltaMin - 2, deltaMax + 2]);

set(gca, 'FontSize', 12, 'LineWidth', 1.5);
legend('Location','northoutside','Orientation','horizontal');

box on   % ensures top border is drawn

%% --- DEFINE THE X-AXIS: 3 DAYS, 6-HOUR TICKS, DAY-BOUNDARY LABELS ---
endLocalNum   = max(timeNum);     % numeric of last data
startLocalNum = endLocalNum - 3;  % 3 days => 3 in datenum

xlim([startLocalNum, endLocalNum]);

% 6-hour increments => 0.25 days
allTicks_6h_num = (startLocalNum : 0.25 : endLocalNum);

% Day boundaries
majorTicks_1d_num = [startLocalNum, startLocalNum+1, ...
                     startLocalNum+2, endLocalNum];

% Place a tick at every 6 hours
axRight.XTick = allTicks_6h_num;

% Label every tick in 'dd-mmm-yy HH:MM'
axRight.XTickLabel = cellstr( datestr(allTicks_6h_num, 'dd-mmm-yy HH:MM') );

% Optionally highlight day boundaries
[~, locMaj] = ismembertol(majorTicks_1d_num, allTicks_6h_num, 1e-8);
for i = 1:numel(locMaj)
    if locMaj(i) ~= 0
        axRight.XTickLabel{locMaj(i)} = datestr(allTicks_6h_num(locMaj(i)), 'dd-mmm-yy HH:MM');
    end
end

% Draw a vertical line at each tick
grid on;

% --- Optionally add bold lines at -1, -2 days from endLocalNum ---
line1 = endLocalNum - 1;
line2 = endLocalNum - 2;

% If xline not supported, use: plot([line1 line1], ylim, 'k', 'LineWidth',2)
xline(line1, 'k', 'LineWidth', 1, 'HandleVisibility', 'off');
xline(line2, 'k', 'LineWidth', 1, 'HandleVisibility', 'off');

%% -------------------------------
% STEP 8: (OPTIONAL) SAVE THE PLOT
% -------------------------------
% saveas(gcf, 'Mstein_Taupoint_Plot.png');

% -------------------------------
% LOCAL FUNCTION
% -------------------------------
function data = fetchAdafruitIOData(baseURL, aioUsername, feedKey, ...
                                    dateStartUTC, dateEndUTC, maxDataPoints)
    % Build base URL
    url = sprintf('%s/%s/feeds/%s/data', baseURL, aioUsername, feedKey);

    % Convert dateStartUTC, dateEndUTC to ISO-8601 strings
    startStr = datestr(dateStartUTC, 'yyyy-mm-ddTHH:MM:SSZ');
    endStr   = datestr(dateEndUTC,   'yyyy-mm-ddTHH:MM:SSZ');

    % Prepare query parameters
    queryParams = { ...
        'limit',      num2str(maxDataPoints), ...
        'start_time', startStr, ...
        'end_time',   endStr ...
    };

    % No API key needed for PUBLIC feeds
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

    % Convert timestamps to datetime (UTC)
    timestampsUTC = datetime({rawData.created_at}, ...
        'InputFormat','yyyy-MM-dd''T''HH:mm:ss''Z''', ...
        'TimeZone','UTC');

    % Convert 'value' to double
    values = str2double({rawData.value});

    % Create timetable
    data = timetable(timestampsUTC', values', 'VariableNames', {'Value'});

    % Filter for desired time window
    data = data(data.Time >= dateStartUTC & data.Time <= dateEndUTC, :);

    % Sort ascending
    data = sortrows(data);
end
