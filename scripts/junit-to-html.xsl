<?xml version="1.0" encoding="UTF-8"?>
<!-- Renders the JUnit XML written by scripts/smoke.py as a standalone HTML summary.
     Usage: xsltproc -o build/smoke-report.html scripts/junit-to-html.xsl build/junit.xml -->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
  <xsl:output method="html" encoding="UTF-8" indent="yes"/>

  <xsl:template match="/testsuites">
    <html lang="en">
      <head>
        <meta charset="utf-8"/>
        <title>MiniOS smoke test report</title>
        <style>
          body { font: 15px/1.5 system-ui, sans-serif; margin: 2rem auto; max-width: 62rem; padding: 0 1rem; color: #1b1f24; }
          h1 { margin-bottom: .25rem; }
          .banner { padding: .75rem 1rem; border-radius: 6px; font-weight: 600; margin: 1rem 0; }
          .banner.pass { background: #dafbe1; color: #116329; }
          .banner.fail { background: #ffebe9; color: #a40e26; }
          table { border-collapse: collapse; width: 100%; }
          th, td { text-align: left; padding: .45rem .6rem; border-bottom: 1px solid #d0d7de; vertical-align: top; }
          td.num { text-align: right; font-variant-numeric: tabular-nums; white-space: nowrap; }
          .status { font-weight: 700; white-space: nowrap; }
          .status.pass { color: #1a7f37; }
          .status.fail { color: #cf222e; }
          details { margin-top: .35rem; }
          pre { background: #f6f8fa; padding: .6rem; overflow-x: auto; max-height: 24rem; font-size: 12px; }
        </style>
      </head>
      <body>
        <h1>MiniOS smoke test report</h1>
        <xsl:for-each select="testsuite">
          <p>
            <xsl:value-of select="@tests"/> boots/cases, run <xsl:value-of select="@timestamp"/>,
            <xsl:value-of select="format-number(@time, '0.0')"/> s total.
          </p>
          <div>
            <xsl:attribute name="class">
              <xsl:choose><xsl:when test="@failures &gt; 0">banner fail</xsl:when><xsl:otherwise>banner pass</xsl:otherwise></xsl:choose>
            </xsl:attribute>
            <xsl:choose>
              <xsl:when test="@failures &gt; 0"><xsl:value-of select="@failures"/> of <xsl:value-of select="@tests"/> failed</xsl:when>
              <xsl:otherwise>All <xsl:value-of select="@tests"/> passed</xsl:otherwise>
            </xsl:choose>
          </div>
          <table>
            <thead><tr><th>Result</th><th>Case</th><th>Seconds</th></tr></thead>
            <tbody>
              <xsl:for-each select="testcase">
                <tr>
                  <td>
                    <xsl:choose>
                      <xsl:when test="failure"><span class="status fail">FAIL</span></xsl:when>
                      <xsl:otherwise><span class="status pass">PASS</span></xsl:otherwise>
                    </xsl:choose>
                  </td>
                  <td>
                    <xsl:value-of select="@name"/>
                    <xsl:if test="failure"><br/><strong><xsl:value-of select="failure/@message"/></strong></xsl:if>
                    <xsl:if test="failure or normalize-space(system-out) != ''">
                      <details>
                        <summary>Output</summary>
                        <pre><xsl:value-of select="failure"/><xsl:text>&#10;</xsl:text><xsl:value-of select="system-out"/></pre>
                      </details>
                    </xsl:if>
                  </td>
                  <td class="num"><xsl:value-of select="format-number(@time, '0.00')"/></td>
                </tr>
              </xsl:for-each>
            </tbody>
          </table>
        </xsl:for-each>
      </body>
    </html>
  </xsl:template>
</xsl:stylesheet>
