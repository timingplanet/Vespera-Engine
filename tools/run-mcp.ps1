param(
    [int]$Port = 46787
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Bridge = Join-Path $Root "tools\vespera_mcp_server.py"

$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) { $Python = Get-Command py -ErrorAction SilentlyContinue }
if (-not $Python) { throw "Python 3 is required to run the Vespera MCP developer bridge." }

& $Python.Source $Bridge --port $Port
