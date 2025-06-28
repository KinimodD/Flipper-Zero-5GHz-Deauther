#pragma once

const char* PORTAL_DEFAULT_TOP = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RTL8720dn-Deauther</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            line-height: 1.6;
            color: #333;
            max-width: 800px;
            margin: 0 auto;
            padding: 20px;
            background-color: #f4f4f4;
        } 
        h1, h2 {
            color: #2c3e50;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin-bottom: 20px;
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background-color: #3498db;
            color: white;
        }
        tr:nth-child(even) {
            background-color: #f2f2f2;
        }
        form {
            background-color: white;
            padding: 20px;
            border-radius: 5px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
            margin-bottom: 20px;
        }
        input[type="submit"] {
            padding: 10px 20px;
            border: none;
            background-color: #3498db;
            color: white;
            border-radius: 4px;
            cursor: pointer;
            transition: background-color 0.3s;
        }
        input[type="submit"]:hover {
            background-color: #2980b9;
        }
    </style>
</head>
<body>
    <h1>RTL8720dn-Deauther</h1>

    <h2>WiFi Networks</h2>
    <form method="post" action="/deauth">
        <table>
            <tr>
                <th>Select</th>
                <th>Number</th>
                <th>SSID</th>
                <th>BSSID</th>
                <th>Channel</th>
                <th>RSSI</th>
                <th>Frequency</th>
            </tr>
)";
// Table rows will be appended in code

const char* PORTAL_DEFAULT_BOTTOM = R"(
    </table>
      <p>Reason code:</p>
      <input type="text" name="reason" placeholder="Reason code">
      <input type="submit" value="Launch Attack">
  </form>

  <form method="post" action="/rescan">
      <input type="submit" value="Rescan networks">
  </form>

  <h2>Reason Codes</h2>
<table>
    <tr>
        <th>Code</th>
        <th>Meaning</th>
    </tr>
    <tr><td>0</td><td>Reserved.</td></tr>
    <tr><td>1</td><td>Unspecified reason.</td></tr>
    <tr><td>2</td><td>Previous authentication no longer valid.</td></tr>
    <tr><td>3</td><td>Deauthenticated because sending station (STA) is leaving or has left Independent Basic Service Set (IBSS) or ESS.</td></tr>
    <tr><td>4</td><td>Disassociated due to inactivity.</td></tr>
    <tr><td>5</td><td>Disassociated because WAP device is unable to handle all currently associated STAs.</td></tr>
    <tr><td>6</td><td>Class 2 frame received from nonauthenticated STA.</td></tr>
    <tr><td>7</td><td>Class 3 frame received from nonassociated STA.</td></tr>
    <tr><td>8</td><td>Disassociated because sending STA is leaving or has left Basic Service Set (BSS).</td></tr>
    <tr><td>9</td><td>STA requesting (re)association is not authenticated with responding STA.</td></tr>
    <tr><td>10</td><td>Disassociated because the information in the Power Capability element is unacceptable.</td></tr>
    <tr><td>11</td><td>Disassociated because the information in the Supported Channels element is unacceptable.</td></tr>
    <tr><td>12</td><td>Disassociated due to BSS Transition Management.</td></tr>
    <tr><td>13</td><td>Invalid element, that is, an element defined in this standard for which the content does not meet the specifications in Clause 8.</td></tr>
    <tr><td>14</td><td>Message integrity code (MIC) failure.</td></tr>
    <tr><td>15</td><td>4-Way Handshake timeout.</td></tr>
    <tr><td>16</td><td>Group Key Handshake timeout.</td></tr>
    <tr><td>17</td><td>Element in 4-Way Handshake different from (Re)Association Request/ Probe Response/Beacon frame.</td></tr>
    <tr><td>18</td><td>Invalid group cipher.</td></tr>
    <tr><td>19</td><td>Invalid pairwise cipher.</td></tr>
    <tr><td>20</td><td>Invalid AKMP.</td></tr>
    <tr><td>21</td><td>Unsupported RSNE version.</td></tr>
    <tr><td>22</td><td>Invalid RSNE capabilities.</td></tr>
    <tr><td>23</td><td>IEEE 802.1X authentication failed.</td></tr>
    <tr><td>24</td><td>Cipher suite rejected because of the security policy.</td></tr>
</table>
</body>
</html>
)";

const char* PORTAL_WAIT = R"(
<!DOCTYPE html>
<html>
<head>
    <meta http-equiv='refresh' content='3;url=/' />
    <title>Scanning...</title>
    <style>
        body { font-family: Arial, sans-serif; background: #f4f4f4; text-align: center; padding-top: 60px; }
        h2 { color: #3498db; }
    </style>
</head>
<body>
    <h2>Scanning WiFi networks, please wait...</h2>
    <p>You may have to re-enter the portal</p>
</body>
</html>
)";
