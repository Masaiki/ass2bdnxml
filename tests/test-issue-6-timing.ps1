param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'

function Read-U16BE([byte[]]$bytes, [int]$offset) {
    return ([int]$bytes[$offset] * 256) + [int]$bytes[$offset + 1]
}

function Read-U32BE([byte[]]$bytes, [int]$offset) {
    return ([uint64]$bytes[$offset] * 16777216) +
        ([uint64]$bytes[$offset + 1] * 65536) +
        ([uint64]$bytes[$offset + 2] * 256) +
        [uint64]$bytes[$offset + 3]
}

function Read-PcsPts([string]$path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $pts = [Collections.Generic.List[uint64]]::new()
    $offset = 0

    while ($offset -lt $bytes.Length) {
        if ($offset + 13 -gt $bytes.Length -or
            $bytes[$offset] -ne 0x50 -or
            $bytes[$offset + 1] -ne 0x47) {
            throw "Invalid PGS segment at offset $offset"
        }

        $length = Read-U16BE $bytes ($offset + 11)
        if ($bytes[$offset + 10] -eq 0x16) {
            $pts.Add((Read-U32BE $bytes ($offset + 2)))
        }
        $offset += 13 + $length
    }

    return $pts.ToArray()
}

function Assert-EqualPts([uint64[]]$actual, [uint64[]]$expected, [string]$mode) {
    $actualText = $actual -join ', '
    $expectedText = $expected -join ', '
    if ($actualText -ne $expectedText) {
        throw "$mode PCS PTS mismatch.`nExpected: $expectedText`nActual:   $actualText"
    }
}

function Assert-FileHashEqual([string]$left, [string]$right, [string]$description) {
    $leftHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $left).Hash
    $rightHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $right).Hash
    if ($leftHash -ne $rightHash) {
        throw "$description differs.`nLeft:  $leftHash`nRight: $rightHash"
    }
}

function Assert-PngSetsEqual([string]$leftDir, [string]$rightDir) {
    $leftFiles = @(Get-ChildItem -LiteralPath $leftDir -Filter '*.png' -File | Sort-Object Name)
    $rightFiles = @(Get-ChildItem -LiteralPath $rightDir -Filter '*.png' -File | Sort-Object Name)
    $leftNames = $leftFiles.Name -join ', '
    $rightNames = $rightFiles.Name -join ', '
    if ($leftNames -ne $rightNames) {
        throw "XML PNG file sets differ.`nLeft:  $leftNames`nRight: $rightNames"
    }

    foreach ($leftFile in $leftFiles) {
        $rightFile = Join-Path $rightDir $leftFile.Name
        Assert-FileHashEqual $leftFile.FullName $rightFile "PNG $($leftFile.Name)"
    }
}

$fixturesDir = Join-Path $PSScriptRoot 'fixtures'
$fixture = Join-Path $fixturesDir 'issue-6-timing.ass'
$tempDir = Join-Path ([IO.Path]::GetTempPath()) ('ass2bdnxml-issue-6-timing-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDir | Out-Null

try {
    $defaultOutput = Join-Path $tempDir 'default.sup'
    & $Executable -v '1440*1080' -f '23.976' -o $defaultOutput $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Default conversion failed with exit code $LASTEXITCODE"
    }

    $frameAlignedPts = @(
        82583, 228979, 379129, 581831, 735735, 908408, 1058558
    )
    Assert-EqualPts (Read-PcsPts $defaultOutput) $frameAlignedPts 'default SUP'

    $defaultSplitOutput = Join-Path $tempDir 'default-split.sup'
    & $Executable -v '1440*1080' -f '23.976' -s 1 -m 1 `
        -o $defaultSplitOutput $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Default split conversion failed with exit code $LASTEXITCODE"
    }
    $defaultSplitPts = Read-PcsPts $defaultSplitOutput
    Assert-EqualPts $defaultSplitPts[0..2] @(82583, 86336, 90090) `
        'default split frame grid'

    $highPrecisionOutput = Join-Path $tempDir 'high-precision.sup'
    & $Executable --sup-high-precision -v '1440*1080' -f '23.976' `
        -o $highPrecisionOutput $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "High-precision conversion failed with exit code $LASTEXITCODE"
    }

    $highPrecisionPts = @(
        79200, 228600, 377100, 579600, 734400, 906300, 1064700
    )
    Assert-EqualPts (Read-PcsPts $highPrecisionOutput) $highPrecisionPts `
        'high-precision SUP'

    $animationFixture = Join-Path $fixturesDir 'issue-6-animation.ass'
    $animationOutput = Join-Path $tempDir 'animation.sup'
    & $Executable --sup-high-precision -v '1440*1080' -f '23.976' `
        -o $animationOutput $animationFixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Animation conversion failed with exit code $LASTEXITCODE"
    }

    $animationPts = Read-PcsPts $animationOutput
    if ($animationPts[0] -ne 79200 -or $animationPts[-1] -ne 117000) {
        throw "Animation boundaries were not preserved: $($animationPts -join ', ')"
    }
    if (82583 -notin $animationPts -or $animationPts.Count -le 2) {
        throw "Animation was not sampled on the video frame timeline: $($animationPts -join ', ')"
    }

    $gapFixture = Join-Path $fixturesDir 'issue-6-gap.ass'
    $gapOutput = Join-Path $tempDir 'gap.sup'
    & $Executable --sup-high-precision -v '1440*1080' -f '23.976' `
        -o $gapOutput $gapFixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Gap conversion failed with exit code $LASTEXITCODE"
    }

    Assert-EqualPts (Read-PcsPts $gapOutput) @(
        79200, 90000, 90900, 108000
    ) 'gap'

    $xmlOnlyDir = Join-Path $tempDir 'xml-only'
    $combinedDir = Join-Path $tempDir 'combined'
    New-Item -ItemType Directory -Path $xmlOnlyDir, $combinedDir | Out-Null
    $xmlOnlyOutput = Join-Path $xmlOnlyDir 'output.xml'
    $combinedXmlOutput = Join-Path $combinedDir 'output.xml'
    $combinedSupOutput = Join-Path $combinedDir 'output.sup'

    & $Executable -v '1440*1080' -f '23.976' -o $xmlOnlyOutput $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "XML-only conversion failed with exit code $LASTEXITCODE"
    }

    & $Executable --sup-high-precision -v '1440*1080' -f '23.976' `
        -o $combinedXmlOutput -o $combinedSupOutput $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Combined conversion failed with exit code $LASTEXITCODE"
    }

    Assert-FileHashEqual $xmlOnlyOutput $combinedXmlOutput 'XML-only and combined XML'
    Assert-PngSetsEqual $xmlOnlyDir $combinedDir
    Assert-EqualPts (Read-PcsPts $combinedSupOutput) $highPrecisionPts 'combined SUP'
    Assert-FileHashEqual $highPrecisionOutput $combinedSupOutput `
        'High-precision SUP-only and combined SUP'

    $defaultCombinedDir = Join-Path $tempDir 'default-combined'
    New-Item -ItemType Directory -Path $defaultCombinedDir | Out-Null
    $defaultCombinedXml = Join-Path $defaultCombinedDir 'output.xml'
    $defaultCombinedSup = Join-Path $defaultCombinedDir 'output.sup'
    & $Executable -v '1440*1080' -f '23.976' `
        -o $defaultCombinedXml -o $defaultCombinedSup $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Default combined conversion failed with exit code $LASTEXITCODE"
    }

    Assert-FileHashEqual $xmlOnlyOutput $defaultCombinedXml `
        'XML-only and default combined XML'
    Assert-PngSetsEqual $xmlOnlyDir $defaultCombinedDir
    Assert-EqualPts (Read-PcsPts $defaultCombinedSup) $frameAlignedPts `
        'default combined SUP'
    Assert-FileHashEqual $defaultOutput $defaultCombinedSup `
        'Default SUP-only and combined SUP'

    $customFpsXmlOnlyDir = Join-Path $tempDir 'custom-fps-xml-only'
    $customFpsCombinedDir = Join-Path $tempDir 'custom-fps-combined'
    New-Item -ItemType Directory -Path $customFpsXmlOnlyDir, $customFpsCombinedDir | Out-Null
    $customFpsXmlOnly = Join-Path $customFpsXmlOnlyDir 'output.xml'
    $customFpsCombinedXml = Join-Path $customFpsCombinedDir 'output.xml'
    $customFpsCombinedSup = Join-Path $customFpsCombinedDir 'output.sup'

    & $Executable -v '1440*1080' -f '7708/133' -o $customFpsXmlOnly $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Custom-FPS XML-only conversion failed with exit code $LASTEXITCODE"
    }

    & $Executable --sup-high-precision -v '1440*1080' -f '7708/133' `
        -o $customFpsCombinedXml -o $customFpsCombinedSup $fixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Custom-FPS combined conversion failed with exit code $LASTEXITCODE"
    }

    Assert-FileHashEqual $customFpsXmlOnly $customFpsCombinedXml `
        'Custom-FPS XML-only and combined XML'
    Assert-PngSetsEqual $customFpsXmlOnlyDir $customFpsCombinedDir

    $emptyRangeXmlOnly = Join-Path $tempDir 'empty-range-only.xml'
    $emptyRangeCombined = Join-Path $tempDir 'empty-range-combined.xml'
    $emptyRangeSup = Join-Path $tempDir 'empty-range.sup'

    & $Executable -v '1440*1080' -f '23.976' -j 28 -n 1 -o $emptyRangeXmlOnly $gapFixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Empty-range XML-only conversion failed with exit code $LASTEXITCODE"
    }

    & $Executable --sup-high-precision -v '1440*1080' -f '23.976' -j 28 -n 1 `
        -o $emptyRangeCombined -o $emptyRangeSup $gapFixture | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Empty-range combined conversion failed with exit code $LASTEXITCODE"
    }

    if ((Test-Path -LiteralPath $emptyRangeXmlOnly) -ne
        (Test-Path -LiteralPath $emptyRangeCombined)) {
        throw 'Combined output changed empty-range XML creation behavior'
    }
    if (-not (Test-Path -LiteralPath $emptyRangeSup)) {
        throw 'Combined output did not preserve the partial-frame SUP output'
    }

    $helpText = (& $Executable --help 2>&1 | Out-String)
    if ($helpText -notmatch 'sup-high-precision') {
        throw '--sup-high-precision option is missing from help output'
    }
}
finally {
    Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}
