[Console]::OutputEncoding = [Text.Encoding]::UTF8
Get-ChildItem Env: | Where-Object Name -match 'PYTHON|PATH' | Select-Object Name,Value
Get-ChildItem 'C:\Users\YSJ\.cache\codex-runtimes\codex-primary-runtime\dependencies\python' -Filter '*pandas*' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 5 -ExpandProperty FullName
