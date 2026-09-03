// ============================================================================
// MuPdfExporter - PDF Export Engine using MuPDF
// ============================================================================

#include "MuPdfExporter.h"

#ifdef SPEEDYNOTE_MUPDF_EXPORT

#include "MuPdfLocks.h"

#include "../core/DarkModeUtils.h"
#include "../core/Document.h"
#include "../core/Page.h"
#include "../layers/VectorLayer.h"
#include "../objects/ImageObject.h"
#include "../objects/LinkObject.h"

#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>

#include <algorithm> // for std::sort
#include <cmath>     // for cosf, sinf, M_PI
#include <functional> // OUT2: std::function for the outline export-index resolver
#include <map>       // for ExtGState alpha cache
#include <unordered_map> // OUT2: notebook-page -> export-index lookup
#include <vector>    // for content stream tokenizer

// Forward declarations for static helper functions defined later in this file
static void appendLayerStrokesToBuffer(fz_context* ctx, pdf_document* outputDoc,
                                       fz_buffer* buf, pdf_obj* resources,
                                       const VectorLayer* layer, qreal pageHeightSn,
                                       int& gsIndex, std::map<int, QString>& alphaToGsName,
                                       bool darkenStrokes = false);
static int getSourcePageRotation(fz_context* ctx, pdf_document* srcPdf, int pageIndex);
static fz_rect getSourcePageBBox(fz_context* ctx, pdf_document* srcPdf, int pageIndex);
// OUT2: build the exported bookmark tree from Document::aggregatedOutline() so the
// export matches the on-screen panel exactly (multi-source grouped roots,
// span-coverage pruning, floor-redirected destinations). exportIndexOf() maps an
// item to its 0-based output page, or -1 for a destination-less header/inert entry.
static pdf_obj* buildAggregatedOutline(fz_context* ctx, pdf_document* outputDoc,
                                       const QVector<PdfOutlineItem>& items,
                                       const std::function<int(const PdfOutlineItem&)>& exportIndexOf);
static bool addImageToPage(fz_context* ctx, pdf_document* outputDoc,
                           const ImageObject* img, fz_buffer* contentBuf,
                           pdf_obj* resources, int imageIndex, float pageHeightPt,
                           const PdfExportOptions& options);

/**
 * @brief Scale factor from SpeedyNote units (96 DPI) to PDF points (72 DPI).
 * 
 * PDF points are 1/72 inch, SpeedyNote uses 96 pixels per inch.
 * Scale = 72/96 = 0.75
 */
static constexpr float SN_TO_PDF_SCALE = 72.0f / 96.0f;

// ============================================================================
// Content Stream Color Rewriting (for vector-preserving dark mode export)
// ============================================================================

namespace {

// How many color operands a given color space requires for sc/SC/scn/SCN.
// Returns -1 if unknown (leave the color unchanged).
static int colorSpaceComponentCount(fz_context* ctx, pdf_obj* csObj)
{
    if (!csObj) return -1;

    if (pdf_name_eq(ctx, csObj, PDF_NAME(DeviceGray)) ||
        pdf_name_eq(ctx, csObj, PDF_NAME(CalGray)))
        return 1;
    if (pdf_name_eq(ctx, csObj, PDF_NAME(DeviceRGB)) ||
        pdf_name_eq(ctx, csObj, PDF_NAME(CalRGB)))
        return 3;
    if (pdf_name_eq(ctx, csObj, PDF_NAME(DeviceCMYK)))
        return 4;

    // Array form: [/ICCBased <stream>], [/CalGray ...], etc.
    if (pdf_is_array(ctx, csObj)) {
        pdf_obj* csName = pdf_array_get(ctx, csObj, 0);
        if (pdf_name_eq(ctx, csName, PDF_NAME(DeviceGray)) ||
            pdf_name_eq(ctx, csName, PDF_NAME(CalGray)))
            return 1;
        if (pdf_name_eq(ctx, csName, PDF_NAME(DeviceRGB)) ||
            pdf_name_eq(ctx, csName, PDF_NAME(CalRGB)))
            return 3;
        if (pdf_name_eq(ctx, csName, PDF_NAME(DeviceCMYK)))
            return 4;
        if (pdf_name_eq(ctx, csName, PDF_NAME(ICCBased))) {
            pdf_obj* profile = pdf_array_get(ctx, csObj, 1);
            if (profile) {
                int n = pdf_dict_get_int(ctx, profile, PDF_NAME(N));
                if (n >= 1 && n <= 4) return n;
            }
        }
    }
    return -1;
}

// Resolve a color space name to its definition in Resources/ColorSpace.
static pdf_obj* resolveColorSpace(fz_context* ctx, pdf_obj* resources, const char* name)
{
    if (!resources || !name) return nullptr;

    // Check for built-in names first
    if (strcmp(name, "DeviceGray") == 0 || strcmp(name, "G") == 0)
        return PDF_NAME(DeviceGray);
    if (strcmp(name, "DeviceRGB") == 0 || strcmp(name, "RGB") == 0)
        return PDF_NAME(DeviceRGB);
    if (strcmp(name, "DeviceCMYK") == 0 || strcmp(name, "CMYK") == 0)
        return PDF_NAME(DeviceCMYK);

    pdf_obj* csDict = pdf_dict_get(ctx, resources, PDF_NAME(ColorSpace));
    if (!csDict) return nullptr;

    pdf_obj* nameObj = pdf_new_name(ctx, name);
    pdf_obj* result = pdf_dict_get(ctx, csDict, nameObj);
    pdf_drop_obj(ctx, nameObj);
    return result;
}

// Invert a grayscale value (0..1).
static inline float invertGray(float g)
{
    return 1.0f - std::clamp(g, 0.0f, 1.0f);
}

// Invert an RGB triplet via HSL lightness inversion.
static void invertRgbHsl(float& r, float& g, float& b)
{
    QColor c = QColor::fromRgbF(std::clamp(r, 0.0f, 1.0f),
                                std::clamp(g, 0.0f, 1.0f),
                                std::clamp(b, 0.0f, 1.0f));
    QColor inv = DarkModeUtils::invertColorLightness(c);
    r = static_cast<float>(inv.redF());
    g = static_cast<float>(inv.greenF());
    b = static_cast<float>(inv.blueF());
}

// Invert a CMYK quadruplet via RGB HSL round-trip.
static void invertCmykHsl(float& c, float& m, float& y, float& k)
{
    // CMYK to RGB (simple formula, adequate for display-intent inversion)
    float r = (1.0f - c) * (1.0f - k);
    float g = (1.0f - m) * (1.0f - k);
    float b = (1.0f - y) * (1.0f - k);
    invertRgbHsl(r, g, b);
    // RGB back to CMYK
    float kk = 1.0f - std::max({r, g, b});
    if (kk >= 1.0f) {
        c = m = y = 0.0f;
        k = 1.0f;
    } else {
        c = (1.0f - r - kk) / (1.0f - kk);
        m = (1.0f - g - kk) / (1.0f - kk);
        y = (1.0f - b - kk) / (1.0f - kk);
        k = kk;
    }
}

// A simple token from a PDF content stream.
struct CsToken {
    enum Type { Number, Name, LiteralString, HexString, Operator, Other };
    Type type;
    QByteArray raw;       // original bytes exactly as they appeared
    float numericValue;   // valid when type == Number
};

// Tokenize a PDF content stream into a list of tokens.
// This is intentionally minimal: it recognises numbers, names, strings,
// operators, and inline images (BI...ID...EI) which are preserved verbatim.
static std::vector<CsToken> tokenizeContentStream(const unsigned char* data, size_t len)
{
    std::vector<CsToken> tokens;
    tokens.reserve(len / 4);

    size_t i = 0;
    auto skipWhitespace = [&]() {
        while (i < len) {
            unsigned char ch = data[i];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\0' || ch == '\f')
                ++i;
            else if (ch == '%') {
                // Skip comment to end of line
                while (i < len && data[i] != '\n' && data[i] != '\r') ++i;
            } else break;
        }
    };

    while (i < len) {
        skipWhitespace();
        if (i >= len) break;

        unsigned char ch = data[i];

        // Number: optional sign, digits, optional decimal point
        if (ch == '+' || ch == '-' || ch == '.' || (ch >= '0' && ch <= '9')) {
            size_t start = i;
            if (ch == '+' || ch == '-') ++i;
            bool hasDot = false;
            while (i < len) {
                unsigned char c = data[i];
                if (c >= '0' && c <= '9') { ++i; continue; }
                if (c == '.' && !hasDot) { hasDot = true; ++i; continue; }
                break;
            }
            if (i == start || (i == start + 1 && (data[start] == '+' || data[start] == '-'))) {
                // Not a valid number (lone sign) — treat as operator
                CsToken tok;
                tok.type = CsToken::Operator;
                tok.raw = QByteArray(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
                tok.numericValue = 0;
                tokens.push_back(tok);
                continue;
            }
            CsToken tok;
            tok.type = CsToken::Number;
            tok.raw = QByteArray(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            tok.numericValue = tok.raw.toFloat();
            tokens.push_back(tok);
            continue;
        }

        // Name: /SomeName
        if (ch == '/') {
            size_t start = i;
            ++i;
            while (i < len) {
                unsigned char c = data[i];
                if (c <= ' ' || c == '/' || c == '(' || c == ')' || c == '<' || c == '>' ||
                    c == '[' || c == ']' || c == '{' || c == '}' || c == '%')
                    break;
                ++i;
            }
            CsToken tok;
            tok.type = CsToken::Name;
            tok.raw = QByteArray(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            tok.numericValue = 0;
            tokens.push_back(tok);
            continue;
        }

        // Literal string: (...)
        if (ch == '(') {
            size_t start = i;
            ++i;
            int depth = 1;
            while (i < len && depth > 0) {
                if (data[i] == '\\') { i = std::min(i + 2, len); continue; }
                if (data[i] == '(') ++depth;
                else if (data[i] == ')') --depth;
                ++i;
            }
            CsToken tok;
            tok.type = CsToken::LiteralString;
            tok.raw = QByteArray(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            tok.numericValue = 0;
            tokens.push_back(tok);
            continue;
        }

        // Hex string: <...>  (but not dictionary << >>)
        if (ch == '<' && (i + 1 >= len || data[i + 1] != '<')) {
            size_t start = i;
            ++i;
            while (i < len && data[i] != '>') ++i;
            if (i < len) ++i; // consume '>'
            CsToken tok;
            tok.type = CsToken::HexString;
            tok.raw = QByteArray(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            tok.numericValue = 0;
            tokens.push_back(tok);
            continue;
        }

        // Dictionary delimiters << >> and array delimiters [ ]
        if ((ch == '<' && i + 1 < len && data[i + 1] == '<') ||
            (ch == '>' && i + 1 < len && data[i + 1] == '>')) {
            CsToken tok;
            tok.type = CsToken::Other;
            tok.raw = QByteArray(reinterpret_cast<const char*>(data + i), 2);
            tok.numericValue = 0;
            tokens.push_back(tok);
            i += 2;
            continue;
        }
        if (ch == '[' || ch == ']' || ch == '>' || ch == '{' || ch == '}') {
            CsToken tok;
            tok.type = CsToken::Other;
            tok.raw = QByteArray(reinterpret_cast<const char*>(data + i), 1);
            tok.numericValue = 0;
            tokens.push_back(tok);
            ++i;
            continue;
        }

        // Operator or keyword (alphabetic sequence, possibly with *)
        {
            size_t start = i;
            while (i < len) {
                unsigned char c = data[i];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    c == '\'' || c == '"' || c == '*')
                    ++i;
                else break;
            }
            if (i == start) {
                // Unknown byte, skip it
                ++i;
                continue;
            }
            CsToken tok;
            tok.type = CsToken::Operator;
            tok.raw = QByteArray(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            tok.numericValue = 0;

            // Handle inline images: BI <key-value pairs> ID <binary data> EI
            if (tok.raw == "BI") {
                // Scan forward to find "ID" operator, then binary data until "EI"
                size_t biStart = start;
                // Find ID: scan tokens until we hit the ID keyword
                while (i < len) {
                    skipWhitespace();
                    if (i >= len) break;
                    if (i + 1 < len && data[i] == 'I' && data[i + 1] == 'D') {
                        i += 2;
                        if (i < len && (data[i] == ' ' || data[i] == '\n' ||
                                        data[i] == '\r' || data[i] == '\t'))
                            ++i;
                        break;
                    }
                    // Skip this token (key or value)
                    size_t tokenStart = i;
                    if (data[i] == '/') {
                        ++i;
                        while (i < len && data[i] > ' ' && data[i] != '/' && data[i] != '(' &&
                               data[i] != ')' && data[i] != '<' && data[i] != '>' &&
                               data[i] != '[' && data[i] != ']') ++i;
                    } else if (data[i] == '(') {
                        ++i;
                        int d = 1;
                        while (i < len && d > 0) {
                            if (data[i] == '\\') { i = std::min(i + 2, len); continue; }
                            if (data[i] == '(') ++d;
                            else if (data[i] == ')') --d;
                            ++i;
                        }
                    } else if (data[i] == '<') {
                        ++i;
                        while (i < len && data[i] != '>') ++i;
                        if (i < len) ++i;
                    } else if (data[i] == '[') {
                        ++i;
                        while (i < len && data[i] != ']') ++i;
                        if (i < len) ++i;
                    } else {
                        while (i < len && data[i] > ' ' && data[i] != '/' && data[i] != '(' &&
                               data[i] != '<' && data[i] != '[') ++i;
                    }
                    if (i == tokenStart) ++i; // safety: always advance
                }
                // Now scan for EI (must be preceded by whitespace and followed by whitespace/EOF)
                while (i < len) {
                    if (i + 1 < len && data[i] == 'E' && data[i + 1] == 'I') {
                        bool prevWs = (i > 0 && (data[i - 1] == ' ' || data[i - 1] == '\n' ||
                                                  data[i - 1] == '\r' || data[i - 1] == '\t'));
                        bool nextWs = (i + 2 >= len || data[i + 2] == ' ' || data[i + 2] == '\n' ||
                                       data[i + 2] == '\r' || data[i + 2] == '\t' ||
                                       data[i + 2] == '%');
                        if (prevWs && nextWs) {
                            i += 2; // consume EI
                            break;
                        }
                    }
                    ++i;
                }
                // Capture the entire BI...EI block as one opaque token
                tok.raw = QByteArray(reinterpret_cast<const char*>(data + biStart), static_cast<int>(i - biStart));
            }

            tokens.push_back(tok);
        }
    }
    return tokens;
}

// Rewrite color operators in a tokenized content stream with HSL lightness inversion.
// Uses a pending-operand approach: number tokens are buffered until we see an
// operator, then either written with inverted values (color op) or verbatim.
// Writes into the caller-provided fz_buffer (which must already be allocated).
static void rewriteContentStreamColors(fz_context* ctx, pdf_obj* resources,
                                       const unsigned char* data, size_t len,
                                       fz_buffer* out)
{
    std::vector<CsToken> tokens = tokenizeContentStream(data, len);

    int fillComponents = -1;
    int strokeComponents = -1;

    auto writeFloat = [&](float v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
        fz_append_string(ctx, out, buf);
    };

    // Flush a token to the output exactly as it appeared
    auto writeToken = [&](const CsToken& t) {
        fz_append_data(ctx, out, reinterpret_cast<const unsigned char*>(t.raw.constData()),
                       t.raw.size());
    };

    // Pending operand stack: indices into the tokens array
    std::vector<size_t> pending;

    auto flushPending = [&]() {
        for (size_t idx : pending) {
            writeToken(tokens[idx]);
            fz_append_byte(ctx, out, ' ');
        }
        pending.clear();
    };

    for (size_t ti = 0; ti < tokens.size(); ++ti) {
        const CsToken& tok = tokens[ti];

        // Accumulate numbers on the pending stack
        if (tok.type == CsToken::Number) {
            pending.push_back(ti);
            continue;
        }

        // Non-operator, non-number tokens: flush pending, write this token
        if (tok.type != CsToken::Operator) {
            flushPending();
            writeToken(tok);
            fz_append_byte(ctx, out, ' ');
            continue;
        }

        // --- Operator token ---

        // Color space tracking: cs / CS
        if (tok.raw == "cs" || tok.raw == "CS") {
            // The last pending token should be a name; but names aren't numbers,
            // so look at the token just before us in the original stream.
            // Actually, the /Name token was already flushed (it's not a Number).
            // We just need to extract the color space name from it.
            if (ti >= 1 && tokens[ti - 1].type == CsToken::Name) {
                const char* csNameStr = tokens[ti - 1].raw.constData() + 1;
                pdf_obj* csObj = resolveColorSpace(ctx, resources, csNameStr);
                int n = colorSpaceComponentCount(ctx, csObj);
                if (tok.raw == "cs") fillComponents = n;
                else strokeComponents = n;
            }
            flushPending();
            writeToken(tok);
            fz_append_byte(ctx, out, '\n');
            continue;
        }

        // DeviceGray: g / G (1 number operand)
        if ((tok.raw == "g" || tok.raw == "G") && pending.size() >= 1) {
            size_t last = pending.size() - 1;
            float gray = tokens[pending[last]].numericValue;
            gray = invertGray(gray);

            // Write all pending except the last one verbatim
            for (size_t j = 0; j < last; ++j) {
                writeToken(tokens[pending[j]]);
                fz_append_byte(ctx, out, ' ');
            }
            pending.clear();
            writeFloat(gray);
            fz_append_byte(ctx, out, ' ');
            writeToken(tok);
            fz_append_byte(ctx, out, '\n');
            if (tok.raw == "g") fillComponents = 1;
            else strokeComponents = 1;
            continue;
        }

        // DeviceRGB: rg / RG (3 number operands)
        if ((tok.raw == "rg" || tok.raw == "RG") && pending.size() >= 3) {
            size_t base = pending.size() - 3;
            float r = tokens[pending[base]].numericValue;
            float g = tokens[pending[base + 1]].numericValue;
            float b = tokens[pending[base + 2]].numericValue;
            invertRgbHsl(r, g, b);

            for (size_t j = 0; j < base; ++j) {
                writeToken(tokens[pending[j]]);
                fz_append_byte(ctx, out, ' ');
            }
            pending.clear();
            writeFloat(r); fz_append_byte(ctx, out, ' ');
            writeFloat(g); fz_append_byte(ctx, out, ' ');
            writeFloat(b); fz_append_byte(ctx, out, ' ');
            writeToken(tok);
            fz_append_byte(ctx, out, '\n');
            if (tok.raw == "rg") fillComponents = 3;
            else strokeComponents = 3;
            continue;
        }

        // DeviceCMYK: k / K (4 number operands)
        if ((tok.raw == "k" || tok.raw == "K") && pending.size() >= 4) {
            size_t base = pending.size() - 4;
            float c = tokens[pending[base]].numericValue;
            float m = tokens[pending[base + 1]].numericValue;
            float y = tokens[pending[base + 2]].numericValue;
            float kk = tokens[pending[base + 3]].numericValue;
            invertCmykHsl(c, m, y, kk);

            for (size_t j = 0; j < base; ++j) {
                writeToken(tokens[pending[j]]);
                fz_append_byte(ctx, out, ' ');
            }
            pending.clear();
            writeFloat(c); fz_append_byte(ctx, out, ' ');
            writeFloat(m); fz_append_byte(ctx, out, ' ');
            writeFloat(y); fz_append_byte(ctx, out, ' ');
            writeFloat(kk); fz_append_byte(ctx, out, ' ');
            writeToken(tok);
            fz_append_byte(ctx, out, '\n');
            if (tok.raw == "k") fillComponents = 4;
            else strokeComponents = 4;
            continue;
        }

        // Generic color: sc / SC / scn / SCN
        if (tok.raw == "sc" || tok.raw == "SC" || tok.raw == "scn" || tok.raw == "SCN") {
            bool isFill = (tok.raw == "sc" || tok.raw == "scn");
            int nComp = isFill ? fillComponents : strokeComponents;

            // For scn/SCN, the last operand might be a pattern name (not in pending)
            // If the token immediately before the operator is a Name, skip inversion.
            bool hasPatternName = false;
            if ((tok.raw == "scn" || tok.raw == "SCN") && ti >= 1 &&
                tokens[ti - 1].type == CsToken::Name) {
                hasPatternName = true;
            }

            if (nComp > 0 && !hasPatternName &&
                pending.size() >= static_cast<size_t>(nComp)) {
                size_t base = pending.size() - static_cast<size_t>(nComp);

                std::vector<float> vals(nComp);
                for (int j = 0; j < nComp; ++j)
                    vals[j] = tokens[pending[base + j]].numericValue;

                if (nComp == 1) vals[0] = invertGray(vals[0]);
                else if (nComp == 3) invertRgbHsl(vals[0], vals[1], vals[2]);
                else if (nComp == 4) invertCmykHsl(vals[0], vals[1], vals[2], vals[3]);

                for (size_t j = 0; j < base; ++j) {
                    writeToken(tokens[pending[j]]);
                    fz_append_byte(ctx, out, ' ');
                }
                pending.clear();
                for (int j = 0; j < nComp; ++j) {
                    writeFloat(vals[j]);
                    fz_append_byte(ctx, out, ' ');
                }
                writeToken(tok);
                fz_append_byte(ctx, out, '\n');
                continue;
            }
        }

        // Default: flush pending as-is, write operator
        flushPending();
        writeToken(tok);
        fz_append_byte(ctx, out, '\n');
    }

    // Flush any remaining pending tokens (shouldn't normally happen)
    flushPending();
}

// Recursively invert colors in a Form XObject and its child Form XObjects.
// Image XObjects (/Subtype /Image) are left untouched.
// For the top-level XObject (isRoot=true), a dark background fill and
// inverted default colors are prepended to handle the implicit white
// page background and the default black graphics state.
static void invertXObjectColors(fz_context* ctx, pdf_document* doc, pdf_obj* xobj,
                                QSet<int>& visited, bool isRoot = false)
{
    if (!xobj) return;

    int objNum = pdf_to_num(ctx, xobj);
    if (objNum == 0 || visited.contains(objNum)) return;
    visited.insert(objNum);

    // Only process Form XObjects
    pdf_obj* subtype = pdf_dict_get(ctx, xobj, PDF_NAME(Subtype));
    if (!pdf_name_eq(ctx, subtype, PDF_NAME(Form))) return;

    pdf_obj* resources = pdf_dict_get(ctx, xobj, PDF_NAME(Resources));

    // Load, rewrite, and store the content stream
    fz_buffer* contentBuf = nullptr;
    fz_buffer* rewritten = nullptr;
    fz_buffer* finalBuf = nullptr;
    fz_try(ctx) {
        contentBuf = pdf_load_stream(ctx, xobj);
        if (contentBuf) {
            unsigned char* data = nullptr;
            size_t dataLen = fz_buffer_storage(ctx, contentBuf, &data);
            if (data && dataLen > 0) {
                rewritten = fz_new_buffer(ctx, dataLen + dataLen / 8 + 256);
                rewriteContentStreamColors(ctx, resources, data, dataLen, rewritten);

                if (isRoot) {
                    // Prepend dark background fill and inverted default colors.
                    // PDF's implicit page background is white and the default
                    // graphics state uses black (0) for both fill and stroke.
                    // Neither appears as an explicit color operator, so the
                    // rewriter cannot invert them. We fix this by:
                    //  1. Filling the BBox with black (inverted white background).
                    //  2. Setting fill/stroke to 1 (inverted default black).
                    pdf_obj* bboxObj = pdf_dict_get(ctx, xobj, PDF_NAME(BBox));
                    fz_rect bbox = bboxObj ? pdf_to_rect(ctx, bboxObj) : fz_make_rect(0, 0, 612, 792);
                    float bw = bbox.x1 - bbox.x0;
                    float bh = bbox.y1 - bbox.y0;

                    finalBuf = fz_new_buffer(ctx, 256 + fz_buffer_storage(ctx, rewritten, nullptr));
                    char preamble[256];
                    snprintf(preamble, sizeof(preamble),
                             "q\n0 g\n%.4f %.4f %.4f %.4f re f\nQ\n"
                             "1 g 1 G 1 1 1 rg 1 1 1 RG\n",
                             bbox.x0, bbox.y0, bw, bh);
                    fz_append_string(ctx, finalBuf, preamble);

                    unsigned char* rwData = nullptr;
                    size_t rwLen = fz_buffer_storage(ctx, rewritten, &rwData);
                    fz_append_data(ctx, finalBuf, rwData, rwLen);

                    pdf_update_stream(ctx, doc, xobj, finalBuf, 0);
                } else {
                    pdf_update_stream(ctx, doc, xobj, rewritten, 0);
                }
            }
        }
    }
    fz_always(ctx) {
        if (contentBuf) fz_drop_buffer(ctx, contentBuf);
        if (rewritten) fz_drop_buffer(ctx, rewritten);
        if (finalBuf) fz_drop_buffer(ctx, finalBuf);
    }
    fz_catch(ctx) {
        qWarning() << "[MuPdfExporter] invertXObjectColors: failed to rewrite stream for obj"
                   << objNum << ":" << fz_caught_message(ctx);
        return;
    }

    // Recurse into child Form XObjects
    if (resources) {
        pdf_obj* xobjDict = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
        if (xobjDict) {
            int n = pdf_dict_len(ctx, xobjDict);
            for (int i = 0; i < n; ++i) {
                pdf_obj* child = pdf_dict_get_val(ctx, xobjDict, i);
                if (!child) continue;
                pdf_obj* childSubtype = pdf_dict_get(ctx, child, PDF_NAME(Subtype));
                if (pdf_name_eq(ctx, childSubtype, PDF_NAME(Form))) {
                    invertXObjectColors(ctx, doc, child, visited);
                }
            }
        }
    }
}

} // anonymous namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

MuPdfExporter::MuPdfExporter(QObject* parent)
    : QObject(parent)
{
}

MuPdfExporter::~MuPdfExporter()
{
    cleanup();
}

// ============================================================================
// Public API
// ============================================================================

void MuPdfExporter::setDocument(Document* document)
{
    m_document = document;
}

PdfExportResult MuPdfExporter::exportPdf(const PdfExportOptions& options)
{
    PdfExportResult result;
    
    // Validate inputs
    if (!m_document) {
        result.errorMessage = tr("No document set for export");
        emit exportFailed(result.errorMessage);
        return result;
    }
    
    if (options.outputPath.isEmpty()) {
        result.errorMessage = tr("No output path specified");
        emit exportFailed(result.errorMessage);
        return result;
    }
    
    // Parse page range
    QVector<int> pageIndices = parsePageRange(options.pageRange, m_document->pageCount());
    if (pageIndices.isEmpty()) {
        result.errorMessage = tr("Invalid page range");
        emit exportFailed(result.errorMessage);
        return result;
    }
    
    m_options = options;
    m_isExporting = true;
    m_cancelled.store(false);
    m_lastError.clear();
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Starting export:"
             << pageIndices.size() << "pages at" << options.dpi << "DPI"
             << "to" << options.outputPath;
    #endif
    
    // Initialize MuPDF
    if (!initContext()) {
        result.errorMessage = tr("Failed to initialize PDF engine");
        cleanup();
        emit exportFailed(result.errorMessage);
        m_isExporting = false;
        return result;
    }
    
    // Open source PDF if document has one
    if (!openSourcePdf()) {
        // Use detailed error message if available
        result.errorMessage = m_lastError.isEmpty() 
            ? tr("Failed to open source PDF") 
            : m_lastError;
        cleanup();
        emit exportFailed(result.errorMessage);
        m_isExporting = false;
        return result;
    }
    
    // Process each page
    int total = static_cast<int>(pageIndices.size());
    for (int i = 0; i < total; ++i) {
        if (m_cancelled.load()) {
            result.errorMessage = tr("Export cancelled");
            cleanup();
            emit exportCancelled();
            m_isExporting = false;
            return result;
        }
        
        int pageIndex = pageIndices[i];
        emit progressUpdated(i + 1, total);
        
        bool pageSuccess = false;
        
        // Determine how to handle this page
        Page* currentPage = m_document->page(pageIndex);
        if (!currentPage) {
            qWarning() << "[MuPdfExporter] Failed to get page" << pageIndex;
            pageSuccess = false;
        } else {
            // Point the active-source aliases at THIS page's own PDF source so that
            // graft/render/import operate on the correct source (multi-source docs).
            QString srcId;
            int pdfPage = -1;
            if (m_document->pdfBindingForNotebookPage(pageIndex, srcId, pdfPage)) {
                activateSource(srcId);
            }

            if (isPageModified(pageIndex)) {
                // Page has annotations - need to render
                if (currentPage->pdfPageNumber >= 0 && m_sourcePdf) {
                    // Modified page with PDF background
                    pageSuccess = renderModifiedPage(pageIndex);
                } else {
                    // No PDF background (blank notebook page)
                    pageSuccess = renderBlankPage(pageIndex);
                }
            } else if (m_sourcePdf && currentPage->pdfPageNumber >= 0) {
                // Unmodified page with PDF
                if (m_options.darkModeBackground) {
                    // Dark mode export requires color rewriting, can't byte-copy
                    pageSuccess = renderModifiedPage(pageIndex);
                } else {
                    pageSuccess = graftPage(pageIndex);
                }
            } else {
                // Unmodified blank page - still need to render
                pageSuccess = renderBlankPage(pageIndex);
            }
        }
        
        if (!pageSuccess) {
            result.errorMessage = tr("Failed to export page %1").arg(pageIndex + 1);
            cleanup();
            emit exportFailed(result.errorMessage);
            m_isExporting = false;
            return result;
        }
        
        result.pagesExported++;
    }
    
    // Metadata and outline are taken from the PRIMARY source; re-activate it since
    // the page loop may have left another source active.
    activateSource(QString());

    // Write metadata
    if (options.preserveMetadata && !writeMetadata()) {
        qWarning() << "[MuPdfExporter] Failed to write metadata (non-fatal)";
    }
    
    // Write outline
    if (options.preserveOutline && !writeOutline(pageIndices)) {
        qWarning() << "[MuPdfExporter] Failed to write outline (non-fatal)";
    }
    
    // Save to disk
    if (!saveDocument(options.outputPath)) {
        result.errorMessage = tr("Failed to save PDF file");
        cleanup();
        
        // Clean up partial output file if it exists
        if (QFile::exists(options.outputPath)) {
            QFile::remove(options.outputPath);
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "[MuPdfExporter] Removed partial output file";
            #endif
        }
        
        emit exportFailed(result.errorMessage);
        m_isExporting = false;
        return result;
    }
    
    // Get file size
    QFileInfo fileInfo(options.outputPath);
    result.fileSizeBytes = fileInfo.size();
    
    // Cleanup and signal success
    cleanup();
    result.success = true;
    m_isExporting = false;
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Export complete:"
             << result.pagesExported << "pages,"
             << (result.fileSizeBytes / 1024) << "KB";
    #endif
    emit exportComplete();
    return result;
}

void MuPdfExporter::cancel()
{
    m_cancelled.store(true);
}

QVector<int> MuPdfExporter::parsePageRange(const QString& rangeString, int totalPages)
{
    QVector<int> result;
    
    if (totalPages <= 0) {
        return result;
    }
    
    QString range = rangeString.trimmed().toLower();
    
    // Empty or "all" means all pages
    if (range.isEmpty() || range == "all") {
        result.reserve(totalPages);
        for (int i = 0; i < totalPages; ++i) {
            result.append(i);
        }
        return result;
    }
    
    // Parse comma-separated parts
    QStringList parts = range.split(',', Qt::SkipEmptyParts);
    QSet<int> seen; // Avoid duplicates
    
    static QRegularExpression rangePattern("^\\s*(\\d+)\\s*-\\s*(\\d+)\\s*$");
    static QRegularExpression singlePattern("^\\s*(\\d+)\\s*$");
    
    for (const QString& part : parts) {
        // Try range pattern (e.g., "1-10")
        QRegularExpressionMatch rangeMatch = rangePattern.match(part);
        if (rangeMatch.hasMatch()) {
            int start = rangeMatch.captured(1).toInt();
            int end = rangeMatch.captured(2).toInt();
            
            // Validate range is within document bounds
            // Return empty (error) if entire range is out of bounds
            if (start > totalPages && end > totalPages) {
                qWarning() << "[MuPdfExporter] Page range" << start << "-" << end 
                           << "is completely out of bounds (document has" << totalPages << "pages)";
                return QVector<int>();  // Invalid range
            }
            if (start < 1 && end < 1) {
                qWarning() << "[MuPdfExporter] Page range" << start << "-" << end << "is invalid";
                return QVector<int>();  // Invalid range
            }
            
            // Clamp partial overlaps (e.g., "1-100" on a 10-page doc exports 1-10)
            start = qMax(1, qMin(start, totalPages));
            end = qMax(1, qMin(end, totalPages));
            
            // Convert to 0-based
            start -= 1;
            end -= 1;
            
            // Handle reversed ranges
            if (start > end) {
                qSwap(start, end);
            }
            
            for (int i = start; i <= end; ++i) {
                if (!seen.contains(i)) {
                    result.append(i);
                    seen.insert(i);
                }
            }
            continue;
        }
        
        // Try single page pattern (e.g., "15")
        QRegularExpressionMatch singleMatch = singlePattern.match(part);
        if (singleMatch.hasMatch()) {
            int page = singleMatch.captured(1).toInt();
            
            // Validate page is within document bounds
            if (page < 1 || page > totalPages) {
                qWarning() << "[MuPdfExporter] Page" << page 
                           << "is out of bounds (document has" << totalPages << "pages)";
                return QVector<int>();  // Invalid page
            }
            
            // Convert to 0-based
            int pageIndex = page - 1;
            
            if (!seen.contains(pageIndex)) {
                result.append(pageIndex);
                seen.insert(pageIndex);
            }
            continue;
        }
        
        // Invalid part - return empty to signal error
        qWarning() << "[MuPdfExporter] Invalid page range part:" << part;
        return QVector<int>();
    }
    
    // Sort the result for consistent ordering
    std::sort(result.begin(), result.end());
    
    return result;
}

// ============================================================================
// Initialization
// ============================================================================

bool MuPdfExporter::initContext()
{
    // Create MuPDF context. The shared lock handlers keep this context safe
    // against concurrent renders elsewhere in the process (BUG-Q003).
    m_ctx = fz_new_context(nullptr, snMuPdfLocks(), FZ_STORE_DEFAULT);
    if (!m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create MuPDF context";
        return false;
    }
    
    // Register document handlers
    fz_try(m_ctx) {
        fz_register_document_handlers(m_ctx);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to register handlers:" << fz_caught_message(m_ctx);
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
        return false;
    }
    
    // Create new PDF document for output
    fz_try(m_ctx) {
        m_outputDoc = pdf_create_document(m_ctx);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create output PDF:" << fz_caught_message(m_ctx);
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
        return false;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Context initialized";
    #endif
    return true;
}

void MuPdfExporter::cleanup()
{
    // Drop every cached source's handles. Graft maps reference both documents, so
    // drop them before the source documents. m_sourcePdf aliases m_sourceDoc, so the
    // source is dropped once via its fz_document handle.
    for (auto& entry : m_sources) {
        SourceHandles& sh = entry.second;
        if (sh.graft) {
            pdf_drop_graft_map(m_ctx, sh.graft);
            sh.graft = nullptr;
        }
        if (sh.doc) {
            fz_drop_document(m_ctx, sh.doc);
            sh.doc = nullptr;
            sh.pdf = nullptr;
        }
    }
    m_sources.clear();
    m_currentSourceId.clear();

    // Active-source aliases are non-owning; just clear them.
    m_sourceDoc = nullptr;
    m_sourcePdf = nullptr;
    m_graftMap = nullptr;
    
    if (m_outputDoc) {
        pdf_drop_document(m_ctx, m_outputDoc);
        m_outputDoc = nullptr;
    }
    
    if (m_ctx) {
        fz_drop_context(m_ctx);
        m_ctx = nullptr;
    }
}

bool MuPdfExporter::openSourcePdf()
{
    if (!m_document) return true;
    
    // Use the same validated absolute/relative candidate resolver as rendering.
    QString pdfPath = m_document->pdfPathForSource(QString());
    if (pdfPath.isEmpty()) {
        // No primary source - this is fine for blank notebooks. Cache an empty entry
        // so activateSource("") is a cheap no-op.
        m_sources[QString()] = SourceHandles{};
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] No primary source PDF (blank document)";
        #endif
        return true;
    }
    
    if (!QFile::exists(pdfPath)) {
        qWarning() << "[MuPdfExporter] Source PDF not found:" << pdfPath;
        m_lastError = tr("Source PDF file not found: %1").arg(pdfPath);
        return false;
    }
    
    QByteArray pathUtf8 = pdfPath.toUtf8();
    SourceHandles sh;
    
    fz_try(m_ctx) {
        sh.doc = fz_open_document(m_ctx, pathUtf8.constData());
        
        // Check for password-protected PDF
        if (fz_needs_password(m_ctx, sh.doc)) {
            qWarning() << "[MuPdfExporter] Source PDF is password-protected";
            fz_drop_document(m_ctx, sh.doc);
            sh.doc = nullptr;
            m_lastError = tr("Cannot export password-protected PDF.\nPlease remove the password and try again.");
            return false;
        }
        
        // Verify it's a PDF (for grafting capabilities)
        sh.pdf = pdf_document_from_fz_document(m_ctx, sh.doc);
        if (!sh.pdf) {
            qWarning() << "[MuPdfExporter] Source is not a PDF document";
            fz_drop_document(m_ctx, sh.doc);
            sh.doc = nullptr;
            m_lastError = tr("Source file is not a valid PDF document.");
            return false;
        }
        
        // Create graft map for efficient multi-page grafting
        // This ensures shared resources (fonts, images) are only copied once
        sh.graft = pdf_new_graft_map(m_ctx, m_outputDoc);
    }
    fz_catch(m_ctx) {
        // sh was not stored yet; drop whatever opened to avoid a leak.
        if (sh.doc) { fz_drop_document(m_ctx, sh.doc); sh.doc = nullptr; sh.pdf = nullptr; }
        qWarning() << "[MuPdfExporter] Failed to open source PDF:" << fz_caught_message(m_ctx);
        m_lastError = tr("Failed to open source PDF: %1").arg(QString::fromUtf8(fz_caught_message(m_ctx)));
        return false;
    }
    
    m_sources[QString()] = sh;
    activateSource(QString());  // make primary the active source
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Opened primary source PDF:" << pdfPath
             << "with" << fz_count_pages(m_ctx, sh.doc) << "pages";
    #endif
    return true;
}

MuPdfExporter::SourceHandles* MuPdfExporter::sourceHandlesFor(const QString& sourceId)
{
    auto it = m_sources.find(sourceId);
    if (it != m_sources.end()) {
        return &it->second;
    }
    
    // Open (and cache) on first use. Failures cache a null-handle entry so we don't
    // retry every page and so callers fall back to blank rendering.
    SourceHandles sh;
    if (m_ctx && m_outputDoc && m_document) {
        QString path = m_document->pdfPathForSource(sourceId);
        if (!path.isEmpty() && QFile::exists(path)) {
            QByteArray pathUtf8 = path.toUtf8();
            fz_document* doc = nullptr;
            fz_var(doc);
            fz_try(m_ctx) {
                doc = fz_open_document(m_ctx, pathUtf8.constData());
                if (!fz_needs_password(m_ctx, doc)) {
                    pdf_document* pdf = pdf_document_from_fz_document(m_ctx, doc);
                    if (pdf) {
                        // Only commit to sh after the graft map is created, so a throw
                        // here leaves ownership with the local `doc` (dropped below).
                        pdf_graft_map* graft = pdf_new_graft_map(m_ctx, m_outputDoc);
                        sh.doc = doc;
                        sh.pdf = pdf;
                        sh.graft = graft;
                        doc = nullptr;  // ownership transferred to sh
                    }
                }
            }
            fz_catch(m_ctx) {
                qWarning() << "[MuPdfExporter] Failed to open source" << sourceId
                           << ":" << fz_caught_message(m_ctx);
            }
            if (doc) {
                // Not adopted (password, not-a-PDF, or graft failure) - drop it.
                fz_drop_document(m_ctx, doc);
            }
        } else if (!path.isEmpty()) {
            qWarning() << "[MuPdfExporter] Source file missing for" << sourceId << ":" << path;
        }
    }
    
    auto res = m_sources.emplace(sourceId, sh);
    return &res.first->second;
}

void MuPdfExporter::activateSource(const QString& sourceId)
{
    m_currentSourceId = sourceId;
    SourceHandles* sh = sourceHandlesFor(sourceId);
    if (sh) {
        m_sourceDoc = sh->doc;
        m_sourcePdf = sh->pdf;
        m_graftMap = sh->graft;
    } else {
        m_sourceDoc = nullptr;
        m_sourcePdf = nullptr;
        m_graftMap = nullptr;
    }
}

// ============================================================================
// Page Processing
// ============================================================================

bool MuPdfExporter::isPageModified(int pageIndex) const
{
    if (!m_document) return true;  // Assume modified if no document
    
    // For PDF export, a page is "modified" if it has ANY content that needs
    // to be rendered on top of (or instead of) the source PDF page.
    //
    // Modified pages require full rendering:
    //   - Strokes in any layer
    //   - Inserted objects (images)
    //
    // Unmodified pages with a PDF background can be "grafted" (byte-copied)
    // directly from the source PDF, which is much faster and preserves
    // the original PDF quality perfectly.
    //
    // Note: We intentionally do NOT use Document::isPageDirty() here.
    // That tracks changes since last save, but for export we need to know
    // if the page has ANY annotations, regardless of when they were made.
    
    Page* page = m_document->page(pageIndex);
    if (!page) return false;  // Non-existent page is not modified
    
    // Use Page's built-in content check (strokes in any layer + objects)
    return page->hasContent();
}

bool MuPdfExporter::graftPage(int pageIndex)
{
    if (!m_sourcePdf || !m_outputDoc || !m_ctx) {
        return false;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page) return false;
    
    // Translate the original page number to the active source's provider index
    // (bundled mini-PDFs remap referenced pages via pageMap).
    int pdfPageNum = m_document->resolveSourcePageIndex(page->pdfSourceId, page->pdfPageNumber);
    if (pdfPageNum < 0) {
        qWarning() << "[MuPdfExporter] Page" << pageIndex << "has no resolvable PDF page number";
        return false;
    }
    
    // Validate source page number is in range
    int srcPageCount = pdf_count_pages(m_ctx, m_sourcePdf);
    if (pdfPageNum >= srcPageCount) {
        qWarning() << "[MuPdfExporter] PDF page" << pdfPageNum 
                   << "out of range (source has" << srcPageCount << "pages)";
        return false;
    }
    
    fz_try(m_ctx) {
        // Use pdf_graft_mapped_page() for efficient page copying
        // This is MuPDF's built-in page grafting function that:
        // 1. Copies the page object and all its resources (fonts, images, etc.)
        // 2. Uses the graft map to avoid duplicating shared resources
        // 3. Properly handles page tree insertion
        //
        // Arguments:
        // - m_graftMap: Reuses resources across multiple grafts
        // - -1: Insert at end of output document
        // - m_sourcePdf: Source document
        // - pdfPageNum: Source page index (0-based)
        pdf_graft_mapped_page(m_ctx, m_graftMap, -1, m_sourcePdf, pdfPageNum);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to graft page" << pageIndex
                   << "(PDF page" << pdfPageNum << "):" << fz_caught_message(m_ctx);
        return false;
    }
    
    return true;
}

bool MuPdfExporter::renderModifiedPage(int pageIndex)
{
    if (!m_outputDoc || !m_ctx || !m_document) {
        return false;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page) return false;
    
    // origPageNum: the ORIGINAL page number stored on the page (used for Document
    // source-aware helpers, which translate to the provider index internally).
    // pdfPageNum: the provider-facing index for direct MuPDF ops against m_sourcePdf
    // (bundled mini-PDFs remap referenced pages via pageMap).
    int origPageNum = page->pdfPageNumber;
    int pdfPageNum = m_document->resolveSourcePageIndex(page->pdfSourceId, origPageNum);
    if (origPageNum < 0 || pdfPageNum < 0 || !m_sourcePdf) {
        // No PDF background - use blank page rendering
        return renderBlankPage(pageIndex);
    }
    
    QSizeF pageSize = page->size;
    
    // Convert page size from SpeedyNote units (96 DPI) to PDF points (72 DPI)
    float widthPt = pageSize.width() * SN_TO_PDF_SCALE;
    float heightPt = pageSize.height() * SN_TO_PDF_SCALE;
    
    // For annotations-only mode, skip PDF background entirely but keep page dimensions
    // This renders only strokes/images on a blank white page
    pdf_obj* bgXObject = nullptr;
    bool bgIsRasterDarkMode = false;  // true when background is a dark-mode raster image
    if (!m_options.annotationsOnly) {
        if (m_options.darkModeBackground && m_options.skipImageMasking) {
            // skipImageMasking: user wants images inverted too, fall back to raster path
            bgIsRasterDarkMode = true;
        } else if (m_options.darkModeBackground) {
            // Vector-preserving dark mode: import as XObject, rewrite colors
            bgXObject = importPageAsXObject(pdfPageNum);
            if (bgXObject) {
                QSet<int> visited;
                invertXObjectColors(m_ctx, m_outputDoc, bgXObject, visited, true);
            } else {
                qWarning() << "[MuPdfExporter] Failed to import PDF page as XObject for dark mode, falling back to raster";
                bgIsRasterDarkMode = true;
            }
        } else {
            // Normal: import the source PDF page as a vector XObject
            bgXObject = importPageAsXObject(pdfPageNum);
            if (!bgXObject) {
                qWarning() << "[MuPdfExporter] Failed to import PDF page as XObject, falling back to blank";
                return renderBlankPage(pageIndex);
            }
        }
    }
    
    // Build combined content stream: background XObject + layers + objects
    fz_buffer* combinedContent = nullptr;
    pdf_obj* resources = nullptr;
    
    fz_try(m_ctx) {
        // Create content buffer
        combinedContent = fz_new_buffer(m_ctx, 1024);
        
        // Create Resources dictionary
        resources = pdf_new_dict(m_ctx, m_outputDoc, 4);
        
        // Dark mode background: rasterize PDF page, invert, embed as image
        if (bgIsRasterDarkMode && m_sourceDoc) {
            QImage bgImage = m_document->renderPdfPageToImage(m_currentSourceId, origPageNum, static_cast<qreal>(m_options.dpi));
            if (!bgImage.isNull()) {
                QVector<QRect> imgRegions;
                if (!m_options.skipImageMasking) {
                    imgRegions = m_document->pdfImageRegions(m_currentSourceId, origPageNum, static_cast<qreal>(m_options.dpi));
                }
                DarkModeUtils::invertImageLightness(bgImage, imgRegions);

                QByteArray compressed = compressImage(bgImage, false,
                    QSizeF(widthPt, heightPt), m_options.dpi);
                if (!compressed.isEmpty()) {
                    fz_buffer* imgBuf = fz_new_buffer_from_copied_data(m_ctx,
                        reinterpret_cast<const unsigned char*>(compressed.constData()),
                        compressed.size());
                    fz_image* fzImg = fz_new_image_from_buffer(m_ctx, imgBuf);
                    fz_drop_buffer(m_ctx, imgBuf);

                    pdf_obj* imgXObj = pdf_add_image(m_ctx, m_outputDoc, fzImg);
                    fz_drop_image(m_ctx, fzImg);

                    pdf_obj* xobjectDict = pdf_dict_get(m_ctx, resources, PDF_NAME(XObject));
                    if (!xobjectDict) {
                        xobjectDict = pdf_new_dict(m_ctx, m_outputDoc, 4);
                        pdf_dict_put(m_ctx, resources, PDF_NAME(XObject), xobjectDict);
                    }
                    pdf_dict_put(m_ctx, xobjectDict, pdf_new_name(m_ctx, "BGDark"), imgXObj);

                    char cmd[128];
                    fz_append_string(m_ctx, combinedContent, "q\n");
                    snprintf(cmd, sizeof(cmd), "%.4f 0 0 %.4f 0 0 cm\n", widthPt, heightPt);
                    fz_append_string(m_ctx, combinedContent, cmd);
                    fz_append_string(m_ctx, combinedContent, "/BGDark Do\n");
                    fz_append_string(m_ctx, combinedContent, "Q\n");
                }
            }
        }

        // Draw background XObject if present (not in annotations-only mode)
        if (bgXObject) {
            // Get source page properties for rotation handling
            // Only needed when drawing the background XObject
            int srcRotation = getSourcePageRotation(m_ctx, m_sourcePdf, pdfPageNum);
            fz_rect srcBBox = getSourcePageBBox(m_ctx, m_sourcePdf, pdfPageNum);
            // Save graphics state, draw background XObject, restore
            // The XObject is referenced as /BGForm in the Resources dictionary
            fz_append_string(m_ctx, combinedContent, "q\n");
            
            // Apply transformation matrix for rotated pages
            // The XObject content is stored "unrotated", but the page had a /Rotate entry
            // We need to apply the rotation when drawing the XObject
            //
            // PDF transformation matrix: [a b c d e f]
            // Represents: x' = ax + cy + e, y' = bx + dy + f
            //
            // For rotation around origin:
            //   0°:   [1 0 0 1 0 0]       (identity)
            //   90°:  [0 1 -1 0 w 0]      (rotate + translate)
            //   180°: [-1 0 0 -1 w h]
            //   270°: [0 -1 1 0 0 h]
            //
            // Where w and h are the page dimensions
            
            if (srcRotation != 0) {
                char matrixCmd[128];
                float bboxW = srcBBox.x1 - srcBBox.x0;
                float bboxH = srcBBox.y1 - srcBBox.y0;
                
                switch (srcRotation) {
                    case 90:
                        // Rotate 90° CW: [0 1 -1 0 bboxH 0]
                        snprintf(matrixCmd, sizeof(matrixCmd), 
                                 "0 1 -1 0 %.4f 0 cm\n", bboxH);
                        break;
                    case 180:
                        // Rotate 180°: [-1 0 0 -1 bboxW bboxH]
                        snprintf(matrixCmd, sizeof(matrixCmd), 
                                 "-1 0 0 -1 %.4f %.4f cm\n", bboxW, bboxH);
                        break;
                    case 270:
                        // Rotate 270° CW (90° CCW): [0 -1 1 0 0 bboxW]
                        snprintf(matrixCmd, sizeof(matrixCmd), 
                                 "0 -1 1 0 0 %.4f cm\n", bboxW);
                        break;
                    default:
                        matrixCmd[0] = '\0';
                        break;
                }
                
                if (matrixCmd[0] != '\0') {
                    fz_append_string(m_ctx, combinedContent, matrixCmd);
                    #ifdef SPEEDYNOTE_DEBUG
                    qDebug() << "[MuPdfExporter] Applied rotation" << srcRotation 
                             << "to page" << pageIndex;
                    #endif
                }
            }
            
            // Handle CropBox offset if it doesn't start at origin
            if (srcBBox.x0 != 0 || srcBBox.y0 != 0) {
                char translateCmd[64];
                snprintf(translateCmd, sizeof(translateCmd), 
                         "1 0 0 1 %.4f %.4f cm\n", -srcBBox.x0, -srcBBox.y0);
                fz_append_string(m_ctx, combinedContent, translateCmd);
            }
            
            // Draw the background XObject
            fz_append_string(m_ctx, combinedContent, "/BGForm Do\n");
            fz_append_string(m_ctx, combinedContent, "Q\n");
            
            // Create XObject subdictionary with background form
            pdf_obj* xobjectDict = pdf_new_dict(m_ctx, m_outputDoc, 4);
            pdf_dict_put(m_ctx, xobjectDict, pdf_new_name(m_ctx, "BGForm"), bgXObject);
            pdf_dict_put(m_ctx, resources, PDF_NAME(XObject), xobjectDict);
        }
        
        // Render content with proper layer affinity ordering:
        // 1. Objects with affinity -1 (below all strokes)
        // 2. Layer 0 strokes
        // 3. Objects with affinity 0
        // 4. Layer 1 strokes
        // 5. Objects with affinity 1
        // ... and so on
        // N. Objects with affinity >= numLayers (always on top)
        
        int imageIndex = 0;
        int gsIndex = 0;  // Counter for ExtGState names (for stroke transparency)
        std::map<int, QString> alphaToGsName;  // Cache: alpha (0-100) -> GS name
        int numLayers = static_cast<int>(page->vectorLayers.size());
        qreal pageHeightSn = page->size.height();
        
        // Save graphics state for strokes/objects
        fz_append_string(m_ctx, combinedContent, "q\n");
        
        // Helper lambda to add objects with a specific affinity (sorted by zOrder)
        auto addObjectsWithAffinity = [&](int affinity) {
            auto it = page->objectsByAffinity.find(affinity);
            if (it == page->objectsByAffinity.end()) return;
            
            // Sort by zOrder (the map stores pointers, not owned objects)
            std::vector<InsertedObject*> sorted = it->second;
            std::sort(sorted.begin(), sorted.end(), 
                      [](const InsertedObject* a, const InsertedObject* b) {
                          return a->zOrder < b->zOrder;
                      });
            
            for (const InsertedObject* obj : sorted) {
                if (obj->type() == QStringLiteral("image")) {
                    const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj);
                    if (imgObj && imgObj->isLoaded()) {
                        addImageToPage(m_ctx, m_outputDoc, imgObj, combinedContent, resources, imageIndex++, heightPt, m_options);
                    }
                }
            }
        };
        
        // 1. Objects with affinity -1 (below all strokes)
        addObjectsWithAffinity(-1);
        
        // 2. Interleave layers and objects
        for (int layerIdx = 0; layerIdx < numLayers; ++layerIdx) {
            // Render this layer's strokes (with transparency support)
            appendLayerStrokesToBuffer(m_ctx, m_outputDoc, combinedContent, resources,
                                      page->vectorLayers[layerIdx].get(), pageHeightSn, gsIndex, alphaToGsName,
                                      m_options.darkenStrokes);
            
            // Render objects with affinity = layerIdx (above this layer, below next)
            addObjectsWithAffinity(layerIdx);
        }
        
        // 3. Objects with affinity >= numLayers (always on top of all strokes)
        for (const auto& [affinity, objects] : page->objectsByAffinity) {
            if (affinity >= numLayers) {
                // Sort by zOrder
                std::vector<InsertedObject*> sorted = objects;
                std::sort(sorted.begin(), sorted.end(), 
                          [](const InsertedObject* a, const InsertedObject* b) {
                              return a->zOrder < b->zOrder;
                          });
                
                for (const InsertedObject* obj : sorted) {
                    if (obj->type() == QStringLiteral("image")) {
                        const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj);
                        if (imgObj && imgObj->isLoaded()) {
                            addImageToPage(m_ctx, m_outputDoc, imgObj, combinedContent, resources, imageIndex++, heightPt, m_options);
                        }
                    }
                }
            }
        }
        
        // Restore graphics state
        fz_append_string(m_ctx, combinedContent, "Q\n");
        
        // Create the page with our resources and content
        fz_rect mediabox = fz_make_rect(0, 0, widthPt, heightPt);
        pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, 
                                         resources, combinedContent);
        
        // Highlights are annotations rather than content, so they attach to the
        // page dictionary instead of the stream built above.
        if (pdf_obj* annots = buildHighlightAnnots(page, pageHeightSn)) {
            pdf_dict_put(m_ctx, pageObj, PDF_NAME(Annots), annots);
        }
        
        pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
    }
    fz_always(m_ctx) {
        if (combinedContent) {
            fz_drop_buffer(m_ctx, combinedContent);
        }
        // Note: resources and bgXObject are owned by the document now, don't drop
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create modified page" << pageIndex
                   << ":" << fz_caught_message(m_ctx);
        return false;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Rendered modified page" << pageIndex 
             << "(PDF page" << pdfPageNum << "+ layers/objects)";
    #endif
    return true;
}

// ============================================================================
// Page Background Rendering (Phase 6)
// ============================================================================

/**
 * @brief Build PDF content stream for page background (color, grid, lines).
 * @param ctx MuPDF context
 * @param page SpeedyNote page with background settings
 * @param widthPt Page width in PDF points
 * @param heightPt Page height in PDF points
 * @return Buffer containing background content stream, or nullptr if no background needed
 * 
 * This renders:
 * - Background color (if not white)
 * - Grid pattern (BackgroundType::Grid)
 * - Ruled lines (BackgroundType::Lines)
 * 
 * Note: Custom background images are handled separately as XObjects.
 */
static fz_buffer* buildBackgroundContentStream(fz_context* ctx, const Page* page,
                                                float widthPt, float heightPt,
                                                bool invertColors = false)
{
    if (!ctx || !page) return nullptr;
    
    // Resolve colours, applying lightness inversion when the caller needs
    // the background flipped (e.g. darkenStrokes exports a dark bg as light)
    QColor bgColor = page->backgroundColor;
    QColor gColor  = page->gridColor;
    if (invertColors) {
        bgColor = DarkModeUtils::invertColorLightness(bgColor);
        gColor  = DarkModeUtils::invertColorLightness(gColor);
    }
    
    // Check if we need to render any background
    bool needsColorFill = (bgColor != Qt::white);
    bool needsGrid = (page->backgroundType == Page::BackgroundType::Grid);
    bool needsLines = (page->backgroundType == Page::BackgroundType::Lines);
    
    if (!needsColorFill && !needsGrid && !needsLines) {
        return nullptr;  // Default white background, nothing to draw
    }
    
    fz_buffer* buf = nullptr;
    
    fz_try(ctx) {
        buf = fz_new_buffer(ctx, 512);
        char cmd[128];
        
        // 1. Fill background color (if not white)
        if (needsColorFill) {
            float r = bgColor.redF();
            float g = bgColor.greenF();
            float b = bgColor.blueF();
            
            snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f rg\n", r, g, b);
            fz_append_string(ctx, buf, cmd);
            
            // Draw filled rectangle covering entire page
            snprintf(cmd, sizeof(cmd), "0 0 %.4f %.4f re f\n", widthPt, heightPt);
            fz_append_string(ctx, buf, cmd);
        }
        
        // 2. Draw grid or lines
        if (needsGrid || needsLines) {
            float r = gColor.redF();
            float g = gColor.greenF();
            float b = gColor.blueF();
            
            snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f RG\n", r, g, b);
            fz_append_string(ctx, buf, cmd);
            
            // Set line width (0.5 pt is a good default for grid lines)
            fz_append_string(ctx, buf, "0.5 w\n");
            
            if (needsGrid) {
                // Grid spacing in PDF points
                float spacingPt = static_cast<float>(page->gridSpacing) * SN_TO_PDF_SCALE;
                if (spacingPt < 1.0f) spacingPt = 10.0f;  // Minimum spacing
                
                // Draw vertical lines (same in both coordinate systems)
                for (float x = spacingPt; x < widthPt; x += spacingPt) {
                    snprintf(cmd, sizeof(cmd), "%.4f 0 m %.4f %.4f l S\n", x, x, heightPt);
                    fz_append_string(ctx, buf, cmd);
                }
                
                // Draw horizontal lines
                // SpeedyNote: first line at y=spacing from top
                // PDF: y=0 is at bottom, so first line is at heightPt - spacingPt
                for (float pdfY = heightPt - spacingPt; pdfY > 0; pdfY -= spacingPt) {
                    snprintf(cmd, sizeof(cmd), "0 %.4f m %.4f %.4f l S\n", pdfY, widthPt, pdfY);
                    fz_append_string(ctx, buf, cmd);
                }
            } else if (needsLines) {
                // Line spacing in PDF points
                float spacingPt = static_cast<float>(page->lineSpacing) * SN_TO_PDF_SCALE;
                if (spacingPt < 1.0f) spacingPt = 10.0f;  // Minimum spacing
                
                // Draw horizontal lines only (ruled paper)
                // SpeedyNote: first line at y=spacing from top
                // PDF: y=0 is at bottom, so first line is at heightPt - spacingPt
                for (float pdfY = heightPt - spacingPt; pdfY > 0; pdfY -= spacingPt) {
                    snprintf(cmd, sizeof(cmd), "0 %.4f m %.4f %.4f l S\n", pdfY, widthPt, pdfY);
                    fz_append_string(ctx, buf, cmd);
                }
            }
        }
    }
    fz_catch(ctx) {
        if (buf) fz_drop_buffer(ctx, buf);
        return nullptr;
    }
    
    return buf;
}

bool MuPdfExporter::renderBlankPage(int pageIndex)
{
    if (!m_outputDoc || !m_ctx) {
        return false;
    }
    
    Page* page = m_document->page(pageIndex);
    if (!page) return false;
    
    QSizeF pageSize = page->size;
    
    // Convert page size from SpeedyNote units (96 DPI) to PDF points (72 DPI)
    float widthPt = pageSize.width() * SN_TO_PDF_SCALE;
    float heightPt = pageSize.height() * SN_TO_PDF_SCALE;
    
    // Build background content stream (color, grid, lines)
    // Skip background in annotations-only mode
    //
    // Dark mode logic for non-PDF pages:
    //   darkModeBackground alone → DON'T invert (user already set a dark bg)
    //   darkenStrokes → DO invert (user wants light export with darkened strokes)
    fz_buffer* backgroundContent = nullptr;
    if (!m_options.annotationsOnly) {
        backgroundContent = buildBackgroundContentStream(m_ctx, page, widthPt, heightPt, m_options.darkenStrokes);
    }
    
    // Check if page has images to add
    bool hasImages = !page->objects.empty();
    
    // Check for custom background image (also skip in annotations-only mode)
    bool hasCustomBackground = !m_options.annotationsOnly &&
                               (page->backgroundType == Page::BackgroundType::Custom && 
                                !page->customBackground.isNull());
    
    // Check if page has strokes
    bool hasStrokes = false;
    for (const auto& layer : page->vectorLayers) {
        if (layer && !layer->strokes().isEmpty()) {
            hasStrokes = true;
            break;
        }
    }
    
    // Determine if we need a combined content buffer
    bool needsCombined = (backgroundContent != nullptr) || hasImages || hasCustomBackground || hasStrokes;
    
    fz_buffer* finalContent = nullptr;
    pdf_obj* resources = nullptr;
    
    fz_try(m_ctx) {
        fz_rect mediabox = fz_make_rect(0, 0, widthPt, heightPt);
        
        if (needsCombined) {
            // Create combined content buffer
            finalContent = fz_new_buffer(m_ctx, 1024);
            
            // Create resources dictionary (needed for custom background, images, or stroke transparency)
            // We create it whenever we have content, as strokes may need ExtGState for transparency
            resources = pdf_new_dict(m_ctx, m_outputDoc, 4);
            
            int imageIndex = 0;
            int gsIndex = 0;  // Counter for ExtGState names (for stroke transparency)
            std::map<int, QString> alphaToGsName;  // Cache: alpha (0-100) -> GS name
            
            // 1. Background color/grid/lines first
            if (backgroundContent) {
                unsigned char* data;
                size_t len = fz_buffer_storage(m_ctx, backgroundContent, &data);
                fz_append_data(m_ctx, finalContent, data, len);
            }
            
            // 2. Custom background image (covers entire page, before strokes)
            if (hasCustomBackground) {
                // Convert QPixmap to QImage
                QImage bgImage = page->customBackground.toImage();
                if (!bgImage.isNull()) {
                    // Compress the background image
                    bool hasAlpha = bgImage.hasAlphaChannel();
                    QSizeF displaySizePt(widthPt, heightPt);
                    QByteArray compressedData = compressImage(bgImage, hasAlpha, displaySizePt, m_options.dpi);
                    
                    if (!compressedData.isEmpty()) {
                        // Create image XObject with proper memory management
                        fz_buffer* imgBuf = nullptr;
                        fz_image* fzImage = nullptr;
                        
                        fz_try(m_ctx) {
                            imgBuf = fz_new_buffer_from_copied_data(m_ctx,
                                reinterpret_cast<const unsigned char*>(compressedData.constData()),
                                compressedData.size());
                            
                            fzImage = fz_new_image_from_buffer(m_ctx, imgBuf);
                            fz_drop_buffer(m_ctx, imgBuf);
                            imgBuf = nullptr;  // Prevent double-drop
                            
                            pdf_obj* imgXObj = pdf_add_image(m_ctx, m_outputDoc, fzImage);
                            fz_drop_image(m_ctx, fzImage);
                            fzImage = nullptr;  // Prevent double-drop
                            
                            // Add to resources
                            pdf_obj* xobjectDict = pdf_dict_get(m_ctx, resources, PDF_NAME(XObject));
                            if (!xobjectDict) {
                                xobjectDict = pdf_new_dict(m_ctx, m_outputDoc, 4);
                                pdf_dict_put(m_ctx, resources, PDF_NAME(XObject), xobjectDict);
                            }
                            
                            char imgName[16];
                            snprintf(imgName, sizeof(imgName), "Img%d", imageIndex++);
                            pdf_dict_put(m_ctx, xobjectDict, pdf_new_name(m_ctx, imgName), imgXObj);
                            
                            // Draw image covering entire page
                            char cmd[128];
                            fz_append_string(m_ctx, finalContent, "q\n");
                            snprintf(cmd, sizeof(cmd), "%.4f 0 0 %.4f 0 0 cm\n", widthPt, heightPt);
                            fz_append_string(m_ctx, finalContent, cmd);
                            snprintf(cmd, sizeof(cmd), "/%s Do\n", imgName);
                            fz_append_string(m_ctx, finalContent, cmd);
                            fz_append_string(m_ctx, finalContent, "Q\n");
                            
                            #ifdef SPEEDYNOTE_DEBUG
                            qDebug() << "[MuPdfExporter] Added custom background image";
                            #endif
                        }
                        fz_always(m_ctx) {
                            if (imgBuf) fz_drop_buffer(m_ctx, imgBuf);
                            if (fzImage) fz_drop_image(m_ctx, fzImage);
                        }
                        fz_catch(m_ctx) {
                            qWarning() << "[MuPdfExporter] Failed to add custom background:" 
                                       << fz_caught_message(m_ctx);
                            // Continue without background (non-fatal)
                        }
                    }
                }
            }
            
            // 3. Render content with proper layer affinity ordering:
            //    - Objects with affinity -1 (below all strokes)
            //    - Layer 0 strokes
            //    - Objects with affinity 0
            //    - Layer 1 strokes
            //    - Objects with affinity 1
            //    - ... and so on
            //    - Objects with affinity >= numLayers (always on top)
            
            int numLayers = static_cast<int>(page->vectorLayers.size());
            qreal pageHeightSn = page->size.height();
            
            // Save graphics state for strokes/objects
            fz_append_string(m_ctx, finalContent, "q\n");
            
            // Helper lambda to add objects with a specific affinity (sorted by zOrder)
            auto addObjectsWithAffinity = [&](int affinity) {
                auto it = page->objectsByAffinity.find(affinity);
                if (it == page->objectsByAffinity.end()) return;
                
                // Sort by zOrder
                std::vector<InsertedObject*> sorted = it->second;
                std::sort(sorted.begin(), sorted.end(), 
                          [](const InsertedObject* a, const InsertedObject* b) {
                              return a->zOrder < b->zOrder;
                          });
                
                for (const InsertedObject* obj : sorted) {
                    if (obj->type() == QStringLiteral("image")) {
                        const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj);
                        if (imgObj && imgObj->isLoaded()) {
                            addImageToPage(m_ctx, m_outputDoc, imgObj, finalContent, resources, imageIndex++, heightPt, m_options);
                        }
                    }
                }
            };
            
            // Objects with affinity -1 (below all strokes)
            addObjectsWithAffinity(-1);
            
            // Interleave layers and objects
            for (int layerIdx = 0; layerIdx < numLayers; ++layerIdx) {
                // Render this layer's strokes (with transparency support)
                appendLayerStrokesToBuffer(m_ctx, m_outputDoc, finalContent, resources,
                                          page->vectorLayers[layerIdx].get(), pageHeightSn, gsIndex, alphaToGsName,
                                          m_options.darkenStrokes);
                
                // Render objects with affinity = layerIdx (above this layer, below next)
                addObjectsWithAffinity(layerIdx);
            }
            
            // Objects with affinity >= numLayers (always on top of all strokes)
            for (const auto& [affinity, objects] : page->objectsByAffinity) {
                if (affinity >= numLayers) {
                    // Sort by zOrder
                    std::vector<InsertedObject*> sorted = objects;
                    std::sort(sorted.begin(), sorted.end(), 
                              [](const InsertedObject* a, const InsertedObject* b) {
                                  return a->zOrder < b->zOrder;
                              });
                    
                    for (const InsertedObject* obj : sorted) {
                        if (obj->type() == QStringLiteral("image")) {
                            const ImageObject* imgObj = dynamic_cast<const ImageObject*>(obj);
                            if (imgObj && imgObj->isLoaded()) {
                                addImageToPage(m_ctx, m_outputDoc, imgObj, finalContent, resources, imageIndex++, heightPt, m_options);
                            }
                        }
                    }
                }
            }
            
            // Restore graphics state
            fz_append_string(m_ctx, finalContent, "Q\n");
            
            // Create page with resources and combined content
            pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, resources, finalContent);
            
            // Highlights are annotations rather than content, so they attach to
            // the page dictionary instead of the stream built above. The empty
            // branch below needs no equivalent: hasImages is really
            // !page->objects.empty(), so any highlight lands here.
            if (pdf_obj* annots = buildHighlightAnnots(page, pageHeightSn)) {
                pdf_dict_put(m_ctx, pageObj, PDF_NAME(Annots), annots);
            }
            
            pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
        } else {
            // Simple path - completely empty page (no strokes, no objects, no background)
            pdf_obj* pageObj = pdf_add_page(m_ctx, m_outputDoc, mediabox, 0, nullptr, nullptr);
            pdf_insert_page(m_ctx, m_outputDoc, -1, pageObj);
        }
    }
    fz_always(m_ctx) {
        if (backgroundContent) {
            fz_drop_buffer(m_ctx, backgroundContent);
        }
        if (finalContent) {
            fz_drop_buffer(m_ctx, finalContent);
        }
        // Note: resources is owned by the page, no need to drop
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create page" << pageIndex
                   << ":" << fz_caught_message(m_ctx);
        return false;
    }
    
    return true;
}

// ============================================================================
// Vector Stroke Conversion (Phase 3)
// ============================================================================

/**
 * @brief Kappa constant for approximating circles with cubic Bezier curves.
 * 
 * A circle can be approximated by 4 cubic Bezier curves. The control points
 * are placed at distance kappa * radius from the arc endpoints.
 * kappa = 4 * (sqrt(2) - 1) / 3 ≈ 0.5522847498
 */
static constexpr float CIRCLE_KAPPA = 0.5522847498f;

/**
 * @brief Transform a point from SpeedyNote coords to PDF coords.
 */
static inline void transformPoint(float& x, float& y, qreal pageHeightSn)
{
    x = x * SN_TO_PDF_SCALE;
    y = static_cast<float>(pageHeightSn - y) * SN_TO_PDF_SCALE;
}

/**
 * @brief Append a polygon subpath to the content stream buffer.
 * 
 * Writes PDF path operators: m (moveto), l (lineto), h (closepath).
 * Does NOT emit f (fill) -- the caller is responsible for emitting a single
 * f after all subpaths are written, so that overlapping shapes (e.g. polygon
 * body + round cap circles) are filled as one composite area without
 * double-compositing semi-transparent alpha.
 */
static void appendPolygonToBuffer(fz_context* ctx, fz_buffer* buf, 
                                   const QPolygonF& polygon, qreal pageHeightSn)
{
    if (polygon.isEmpty()) return;
    
    char cmd[64];
    
    // Move to first point
    float x = static_cast<float>(polygon[0].x());
    float y = static_cast<float>(polygon[0].y());
    transformPoint(x, y, pageHeightSn);
    snprintf(cmd, sizeof(cmd), "%.4f %.4f m\n", x, y);
    fz_append_string(ctx, buf, cmd);
    
    // Line to remaining points
    for (int i = 1; i < polygon.size(); ++i) {
        x = static_cast<float>(polygon[i].x());
        y = static_cast<float>(polygon[i].y());
        transformPoint(x, y, pageHeightSn);
        snprintf(cmd, sizeof(cmd), "%.4f %.4f l\n", x, y);
        fz_append_string(ctx, buf, cmd);
    }
    
    // Close subpath (caller emits a single 'f' after all subpaths are written)
    fz_append_string(ctx, buf, "h\n");
}

/**
 * @brief Append a circle subpath to the content stream buffer.
 * 
 * Approximates a circle using 4 cubic Bezier curves (standard PDF technique).
 * Uses operators: m (moveto), c (curveto), h (closepath).
 * Does NOT emit f (fill) -- see appendPolygonToBuffer for rationale.
 */
static void appendCircleToBuffer(fz_context* ctx, fz_buffer* buf,
                                  const QPointF& center, qreal radius, qreal pageHeightSn)
{
    if (radius <= 0) return;
    
    // Transform center to PDF coords
    float cx = static_cast<float>(center.x());
    float cy = static_cast<float>(center.y());
    transformPoint(cx, cy, pageHeightSn);
    float r = static_cast<float>(radius) * SN_TO_PDF_SCALE;
    
    // Control point offset for Bezier approximation
    float k = r * CIRCLE_KAPPA;
    
    char cmd[128];
    
    // Start at right point of circle (3 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f m\n", cx + r, cy);
    fz_append_string(ctx, buf, cmd);
    
    // Top-right quadrant (to 12 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
             cx + r, cy + k,      // control point 1
             cx + k, cy + r,      // control point 2
             cx, cy + r);         // end point
    fz_append_string(ctx, buf, cmd);
    
    // Top-left quadrant (to 9 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
             cx - k, cy + r,
             cx - r, cy + k,
             cx - r, cy);
    fz_append_string(ctx, buf, cmd);
    
    // Bottom-left quadrant (to 6 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
             cx - r, cy - k,
             cx - k, cy - r,
             cx, cy - r);
    fz_append_string(ctx, buf, cmd);
    
    // Bottom-right quadrant (back to 3 o'clock)
    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f %.4f %.4f c\n",
             cx + k, cy - r,
             cx + r, cy - k,
             cx + r, cy);
    fz_append_string(ctx, buf, cmd);
    
    // Close subpath (caller emits a single 'f' after all subpaths are written)
    fz_append_string(ctx, buf, "h\n");
}

// NOTE: This function is currently unused. The implementation uses content stream operators
// (via appendPolygonToBuffer) instead of fz_path objects. Kept for potential future use
// with fz_fill_path() on a drawing device if needed for advanced rendering scenarios.
fz_path* MuPdfExporter::polygonToPath(const QPolygonF& polygon, qreal pageHeightSn)
{
    if (polygon.isEmpty() || !m_ctx) {
        return nullptr;
    }
    
    fz_path* path = nullptr;
    
    fz_try(m_ctx) {
        path = fz_new_path(m_ctx);
        
        // Transform first point and move to it
        // SpeedyNote: origin at top-left, 96 DPI
        // PDF: origin at bottom-left, 72 DPI (points)
        float x = static_cast<float>(polygon[0].x()) * SN_TO_PDF_SCALE;
        float y = static_cast<float>(pageHeightSn - polygon[0].y()) * SN_TO_PDF_SCALE;
        fz_moveto(m_ctx, path, x, y);
        
        // Line to remaining points with same transformation
        for (int i = 1; i < polygon.size(); ++i) {
            x = static_cast<float>(polygon[i].x()) * SN_TO_PDF_SCALE;
            y = static_cast<float>(pageHeightSn - polygon[i].y()) * SN_TO_PDF_SCALE;
            fz_lineto(m_ctx, path, x, y);
        }
        
        // Close the path (for filled polygons)
        fz_closepath(m_ctx, path);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to create path:" << fz_caught_message(m_ctx);
        if (path) {
            fz_drop_path(m_ctx, path);
        }
        return nullptr;
    }
    
    return path;
}

/**
 * @brief Get or create an ExtGState resource for a given alpha value.
 * @param ctx MuPDF context
 * @param outputDoc Output PDF document
 * @param resources Resources dictionary to add ExtGState to
 * @param alpha The fill alpha value (0.0 to 1.0)
 * @param gsIndex Current graphics state index (will be incremented only if new entry created)
 * @param alphaToGsName Cache mapping alpha values to existing GS names (for reuse)
 * @return The name of the ExtGState (e.g., "GS0", "GS1", etc.) or empty if alpha is 1.0
 * 
 * Creates an ExtGState dictionary with:
 *   /Type /ExtGState
 *   /ca <alpha>   (fill alpha)
 * 
 * The ExtGState is added to resources under /ExtGState/<name>.
 * 
 * OPTIMIZATION: Caches ExtGState entries by alpha value (quantized to 2 decimal places).
 * Multiple strokes with the same opacity reuse the same ExtGState entry.
 */
static QString getOrCreateExtGState(fz_context* ctx, pdf_document* outputDoc,
                                     pdf_obj* resources, float alpha, int& gsIndex,
                                     std::map<int, QString>& alphaToGsName)
{
    // If fully opaque, no need for ExtGState
    if (alpha >= 0.999f) {
        return QString();
    }
    
    // Clamp alpha to valid range
    alpha = qBound(0.0f, alpha, 1.0f);
    
    // Quantize alpha to 2 decimal places (0-100 integer key)
    // This avoids creating separate entries for alpha 0.501 vs 0.502
    int alphaKey = qRound(alpha * 100.0f);
    
    // Check if we already have an ExtGState for this alpha
    auto it = alphaToGsName.find(alphaKey);
    if (it != alphaToGsName.end()) {
        return it->second;  // Reuse existing ExtGState
    }
    
    // Generate unique name for this graphics state
    QString gsName = QStringLiteral("GS%1").arg(gsIndex++);
    QByteArray gsNameUtf8 = gsName.toUtf8();
    
    // Get or create ExtGState dictionary in resources
    pdf_obj* extGStateDict = pdf_dict_get(ctx, resources, PDF_NAME(ExtGState));
    if (!extGStateDict) {
        extGStateDict = pdf_new_dict(ctx, outputDoc, 4);
        pdf_dict_put(ctx, resources, PDF_NAME(ExtGState), extGStateDict);
    }
    
    // Create the graphics state dictionary
    pdf_obj* gsDict = pdf_new_dict(ctx, outputDoc, 2);
    pdf_dict_put(ctx, gsDict, PDF_NAME(Type), PDF_NAME(ExtGState));
    pdf_dict_put_real(ctx, gsDict, PDF_NAME(ca), alpha);  // Fill alpha (lowercase 'ca')
    
    // Add to ExtGState dictionary
    pdf_dict_put(ctx, extGStateDict, pdf_new_name(ctx, gsNameUtf8.constData()), gsDict);
    
    // Cache for reuse
    alphaToGsName[alphaKey] = gsName;
    
    return gsName;
}

/**
 * @brief Append a single layer's strokes to the content buffer.
 * @param ctx MuPDF context
 * @param outputDoc Output PDF document (for creating ExtGState resources)
 * @param buf Buffer to append to (must not be null)
 * @param resources Resources dictionary (for adding ExtGState entries)
 * @param layer The vector layer to render
 * @param pageHeightSn Page height in SpeedyNote coordinates (for Y-flip)
 * @param gsIndex Current graphics state index counter (modified by function)
 * @param alphaToGsName Cache for ExtGState names by alpha value (for reuse)
 * 
 * This is used by the interleaved rendering to render layers one at a time,
 * allowing objects to be inserted between layers based on their affinity.
 * 
 * Opacity handling:
 * - Layer opacity is applied to all strokes in the layer
 * - Stroke color alpha is multiplied with layer opacity
 * - Total alpha < 1.0 creates an ExtGState with fill alpha (ca)
 */
static void appendLayerStrokesToBuffer(fz_context* ctx, pdf_document* outputDoc,
                                       fz_buffer* buf, pdf_obj* resources,
                                       const VectorLayer* layer, qreal pageHeightSn,
                                       int& gsIndex, std::map<int, QString>& alphaToGsName,
                                       bool darkenStrokes)
{
    if (!ctx || !buf || !layer) return;
    
    if (!layer->visible || layer->strokes().isEmpty()) {
        return;
    }
    
    // Get layer opacity
    float layerOpacity = static_cast<float>(layer->opacity);
    
    for (const VectorStroke& stroke : layer->strokes()) {
        // Build the stroke polygon using existing VectorLayer logic
        VectorLayer::StrokePolygonResult polyResult = VectorLayer::buildStrokePolygon(stroke);
        
        // Calculate effective alpha (stroke alpha × layer opacity)
        float strokeAlpha = static_cast<float>(stroke.color.alphaF());
        float effectiveAlpha = strokeAlpha * layerOpacity;
        bool needsTransparency = (effectiveAlpha < 0.999f) && resources && outputDoc;
        
        // Save graphics state if using transparency (so we can restore after)
        if (needsTransparency) {
            fz_append_string(ctx, buf, "q\n");
            
            // Apply transparency via ExtGState (reuses existing entry if same alpha)
            QString gsName = getOrCreateExtGState(ctx, outputDoc, resources, effectiveAlpha, gsIndex, alphaToGsName);
            if (!gsName.isEmpty()) {
                char gsCmd[32];
                snprintf(gsCmd, sizeof(gsCmd), "/%s gs\n", gsName.toUtf8().constData());
                fz_append_string(ctx, buf, gsCmd);
            }
        }
        
        // Set fill color, optionally darkening light strokes for export
        QColor strokeColor = stroke.color;
        if (darkenStrokes) {
            strokeColor = DarkModeUtils::darkenColorForExport(strokeColor);
        }
        float r = strokeColor.redF();
        float g = strokeColor.greenF();
        float b = strokeColor.blueF();
        
        char colorCmd[64];
        snprintf(colorCmd, sizeof(colorCmd), "%.4f %.4f %.4f rg\n", r, g, b);
        fz_append_string(ctx, buf, colorCmd);
        
        if (polyResult.isSinglePoint) {
            appendCircleToBuffer(ctx, buf, polyResult.startCapCenter, 
                                polyResult.startCapRadius, pageHeightSn);
            fz_append_string(ctx, buf, "f\n");
        } else if (!polyResult.polygon.isEmpty()) {
            appendPolygonToBuffer(ctx, buf, polyResult.polygon, pageHeightSn);
            
            if (polyResult.hasRoundCaps) {
                appendCircleToBuffer(ctx, buf, polyResult.startCapCenter,
                                    polyResult.startCapRadius, pageHeightSn);
                appendCircleToBuffer(ctx, buf, polyResult.endCapCenter,
                                    polyResult.endCapRadius, pageHeightSn);
            }
            // Single fill for all subpaths (polygon + caps) to prevent
            // double-opacity at cap/body overlap for semi-transparent strokes
            fz_append_string(ctx, buf, "f\n");
        }
        
        // Restore graphics state if we saved it for transparency
        if (needsTransparency) {
            fz_append_string(ctx, buf, "Q\n");
        }
    }
}

// ============================================================================
// PDF Background (Phase 4)
// ============================================================================

/**
 * @brief Get the rotation value of a source PDF page.
 * @param ctx MuPDF context
 * @param srcPdf Source PDF document
 * @param pageIndex Page index (0-based)
 * @return Rotation in degrees (0, 90, 180, or 270), normalized
 */
static int getSourcePageRotation(fz_context* ctx, pdf_document* srcPdf, int pageIndex)
{
    int rotation = 0;
    
    fz_try(ctx) {
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, srcPdf, pageIndex);
        pdf_obj* rotateObj = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Rotate));
        if (rotateObj) {
            rotation = pdf_to_int(ctx, rotateObj);
            // Normalize to 0, 90, 180, or 270
            rotation = ((rotation % 360) + 360) % 360;
            // Only accept valid rotation values
            if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
                rotation = 0;
            }
        }
    }
    fz_catch(ctx) {
        rotation = 0;
    }
    
    return rotation;
}

/**
 * @brief Get the BBox of a source PDF page (CropBox or MediaBox).
 * @param ctx MuPDF context
 * @param srcPdf Source PDF document
 * @param pageIndex Page index (0-based)
 * @return Page bounds as fz_rect
 */
static fz_rect getSourcePageBBox(fz_context* ctx, pdf_document* srcPdf, int pageIndex)
{
    fz_rect bbox = fz_empty_rect;
    
    fz_try(ctx) {
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, srcPdf, pageIndex);
        
        // Try CropBox first, fall back to MediaBox
        pdf_obj* boxObj = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(CropBox));
        if (!boxObj) {
            boxObj = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(MediaBox));
        }
        
        if (boxObj) {
            bbox = pdf_to_rect(ctx, boxObj);
        }
    }
    fz_catch(ctx) {
        bbox = fz_empty_rect;
    }
    
    return bbox;
}

pdf_obj* MuPdfExporter::importPageAsXObject(int sourcePageIndex)
{
    // Validate we have source PDF and output document
    if (!m_sourcePdf || !m_outputDoc || !m_ctx) {
        qWarning() << "[MuPdfExporter] importPageAsXObject: No source PDF or output document";
        return nullptr;
    }
    
    // Validate page index
    int srcPageCount = pdf_count_pages(m_ctx, m_sourcePdf);
    if (sourcePageIndex < 0 || sourcePageIndex >= srcPageCount) {
        qWarning() << "[MuPdfExporter] importPageAsXObject: Page" << sourcePageIndex
                   << "out of range (source has" << srcPageCount << "pages)";
        return nullptr;
    }
    
    pdf_obj* xobj = nullptr;
    
    fz_try(m_ctx) {
        // Load the source page object
        pdf_obj* srcPageObj = pdf_lookup_page_obj(m_ctx, m_sourcePdf, sourcePageIndex);
        
        // Get page properties
        // MediaBox defines the page coordinate system
        pdf_obj* mediaBox = pdf_dict_get_inheritable(m_ctx, srcPageObj, PDF_NAME(MediaBox));
        if (!mediaBox) {
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "Source page has no MediaBox");
        }
        
        // Get CropBox if it exists (actual visible area), otherwise use MediaBox
        pdf_obj* cropBox = pdf_dict_get_inheritable(m_ctx, srcPageObj, PDF_NAME(CropBox));
        pdf_obj* bbox = cropBox ? cropBox : mediaBox;
        
        // Get page Resources (fonts, images, color spaces, etc.)
        pdf_obj* srcResources = pdf_dict_get_inheritable(m_ctx, srcPageObj, PDF_NAME(Resources));
        
        // Get page Contents stream(s)
        pdf_obj* srcContents = pdf_dict_get(m_ctx, srcPageObj, PDF_NAME(Contents));
        
        // Create the Form XObject dictionary in output document
        xobj = pdf_new_dict(m_ctx, m_outputDoc, 8);
        
        // Set required Form XObject properties
        pdf_dict_put(m_ctx, xobj, PDF_NAME(Type), PDF_NAME(XObject));
        pdf_dict_put(m_ctx, xobj, PDF_NAME(Subtype), PDF_NAME(Form));
        pdf_dict_put(m_ctx, xobj, PDF_NAME(FormType), pdf_new_int(m_ctx, 1));
        
        // Copy BBox (use graft to handle indirect references)
        pdf_obj* graftedBBox = pdf_graft_mapped_object(m_ctx, m_graftMap, bbox);
        pdf_dict_put(m_ctx, xobj, PDF_NAME(BBox), graftedBBox);
        
        // Copy Resources (graft to resolve references and copy fonts/images)
        if (srcResources) {
            pdf_obj* graftedResources = pdf_graft_mapped_object(m_ctx, m_graftMap, srcResources);
            pdf_dict_put(m_ctx, xobj, PDF_NAME(Resources), graftedResources);
        }
        
        // Add the XObject to the output document's object table FIRST
        // This converts it from a direct object to an indirect object with a proper
        // object number. This must be done before pdf_update_stream() which requires
        // an indirect object.
        //
        // BUG FIX: Previously we used pdf_update_object(ctx, doc, pdf_to_num(xobj), xobj)
        // but pdf_to_num() returns 0 for direct objects (no object number assigned yet),
        // which caused the XObject to overwrite the root object, breaking the PDF.
        xobj = pdf_add_object(m_ctx, m_outputDoc, xobj);
        
        // Handle page contents
        // Contents can be a single stream or an array of streams
        if (srcContents) {
            fz_buffer* contentBuf = nullptr;
            
            fz_try(m_ctx) {
                if (pdf_is_array(m_ctx, srcContents)) {
                    // Multiple content streams - concatenate them
                    contentBuf = fz_new_buffer(m_ctx, 1024);
                    
                    int numStreams = pdf_array_len(m_ctx, srcContents);
                    for (int i = 0; i < numStreams; ++i) {
                        pdf_obj* stream = pdf_array_get(m_ctx, srcContents, i);
                        fz_buffer* streamBuf = pdf_load_stream(m_ctx, stream);
                        if (streamBuf) {
                            // Add space between streams
                            if (i > 0) {
                                fz_append_byte(m_ctx, contentBuf, ' ');
                            }
                            fz_append_buffer(m_ctx, contentBuf, streamBuf);
                            fz_drop_buffer(m_ctx, streamBuf);
                        }
                    }
                } else {
                    // Single content stream - copy it directly
                    contentBuf = pdf_load_stream(m_ctx, srcContents);
                }
                
                // Add the content stream to the XObject (now an indirect object)
                if (contentBuf) {
                    pdf_update_stream(m_ctx, m_outputDoc, xobj, contentBuf, 0);
                }
            }
            fz_always(m_ctx) {
                if (contentBuf) fz_drop_buffer(m_ctx, contentBuf);
            }
            fz_catch(m_ctx) {
                fz_rethrow(m_ctx);  // Re-throw to outer catch block
            }
        }
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] importPageAsXObject failed:"
                   << fz_caught_message(m_ctx);
        // Don't drop xobj here - it may have been partially added to document
        return nullptr;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Imported page" << sourcePageIndex << "as XObject";
    #endif
    return xobj;
}

// ============================================================================
// Image Handling (Phase 5 - TODO)
// ============================================================================

static bool addImageToPage(fz_context* ctx, pdf_document* outputDoc,
                           const ImageObject* img, fz_buffer* contentBuf,
                           pdf_obj* resources, int imageIndex, float pageHeightPt,
                           const PdfExportOptions& options)
{
    if (!img || !contentBuf || !resources || !ctx || !outputDoc) {
        return false;
    }
    
    // Check if image is loaded
    if (!img->isLoaded() || img->pixmap().isNull()) {
        qWarning() << "[MuPdfExporter] Image not loaded:" << img->imagePath;
        return false;
    }
    
    // Skip invisible images
    if (!img->visible) {
        return true;
    }
    
    // Get image data
    QImage qimg = img->pixmap().toImage();
    if (qimg.isNull()) {
        qWarning() << "[MuPdfExporter] Failed to convert pixmap to image";
        return false;
    }
    
    // Determine if image has alpha
    bool hasAlpha = qimg.hasAlphaChannel();
    
    // Calculate display size in PDF points
    // SpeedyNote uses 96 DPI, PDF uses 72 DPI
    float displayWidthPt = static_cast<float>(img->size.width()) * SN_TO_PDF_SCALE;
    float displayHeightPt = static_cast<float>(img->size.height()) * SN_TO_PDF_SCALE;
    
    // Skip zero-size images (would cause invalid transformation matrix)
    if (displayWidthPt <= 0 || displayHeightPt <= 0) {
        qWarning() << "[MuPdfExporter] Skipping zero-size image";
        return true;
    }
    
    QSizeF displaySizePt(displayWidthPt, displayHeightPt);
    
    // Compress with downsampling
    QByteArray compressedData = MuPdfExporter::compressImage(qimg, hasAlpha, displaySizePt, options.dpi);
    if (compressedData.isEmpty()) {
        qWarning() << "[MuPdfExporter] Failed to compress image";
        return false;
    }
    
    fz_buffer* imgBuf = nullptr;
    fz_image* fzImage = nullptr;
    
    fz_try(ctx) {
        // Create image from compressed data
        imgBuf = fz_new_buffer_from_copied_data(ctx, 
            reinterpret_cast<const unsigned char*>(compressedData.constData()),
            compressedData.size());
        
        fzImage = fz_new_image_from_buffer(ctx, imgBuf);
        fz_drop_buffer(ctx, imgBuf);
        imgBuf = nullptr;  // Prevent double-drop in fz_always
        
        // Add image to PDF as XObject
        pdf_obj* imgXObj = pdf_add_image(ctx, outputDoc, fzImage);
        fz_drop_image(ctx, fzImage);
        fzImage = nullptr;  // Prevent double-drop in fz_always
        
        // Get or create XObject dictionary in resources
        pdf_obj* xobjectDict = pdf_dict_get(ctx, resources, PDF_NAME(XObject));
        if (!xobjectDict) {
            xobjectDict = pdf_new_dict(ctx, outputDoc, 4);
            pdf_dict_put(ctx, resources, PDF_NAME(XObject), xobjectDict);
        }
        
        // Add image XObject with unique name
        char imgName[16];
        snprintf(imgName, sizeof(imgName), "Img%d", imageIndex);
        pdf_dict_put(ctx, xobjectDict, pdf_new_name(ctx, imgName), imgXObj);
        
        // Build transformation matrix for position, scale, and rotation
        // PDF image XObjects are 1x1 unit, so we need to scale to display size
        // Position is relative to page origin (bottom-left in PDF)
        
        float posX = static_cast<float>(img->position.x()) * SN_TO_PDF_SCALE;
        float posY = static_cast<float>(img->position.y()) * SN_TO_PDF_SCALE;
        
        // Convert Y from top-left origin to bottom-left origin
        // The image's top-left corner in PDF coords
        float pdfY = pageHeightPt - posY - displayHeightPt;
        
        // Append drawing commands to content buffer
        fz_append_string(ctx, contentBuf, "q\n");  // Save graphics state
        
        if (img->rotation != 0.0) {
            // For rotation, we need to:
            // 1. Translate to image center
            // 2. Rotate
            // 3. Translate back
            // 4. Scale and position
            
            float centerX = posX + displayWidthPt / 2.0f;
            float centerY = pdfY + displayHeightPt / 2.0f;
            
            // Negate rotation angle to account for Y-axis flip
            // SpeedyNote: Y increases downward, positive rotation = counterclockwise
            // PDF: Y increases upward, so we need to negate to preserve visual rotation direction
            float radians = static_cast<float>(-img->rotation * M_PI / 180.0);
            float cosR = cosf(radians);
            float sinR = sinf(radians);
            
            // Combined matrix: translate to center, rotate, translate back, then scale/position
            // This is complex, so let's build it step by step in the content stream
            char cmd[256];
            
            // Translate to center, rotate, translate back
            snprintf(cmd, sizeof(cmd), 
                     "1 0 0 1 %.4f %.4f cm\n",  // Translate to center
                     centerX, centerY);
            fz_append_string(ctx, contentBuf, cmd);
            
            snprintf(cmd, sizeof(cmd),
                     "%.4f %.4f %.4f %.4f 0 0 cm\n",  // Rotate
                     cosR, sinR, -sinR, cosR);
            fz_append_string(ctx, contentBuf, cmd);
            
            snprintf(cmd, sizeof(cmd),
                     "1 0 0 1 %.4f %.4f cm\n",  // Translate back
                     -displayWidthPt / 2.0f, -displayHeightPt / 2.0f);
            fz_append_string(ctx, contentBuf, cmd);
            
            // Scale to display size (image XObject is 1x1)
            snprintf(cmd, sizeof(cmd),
                     "%.4f 0 0 %.4f 0 0 cm\n",
                     displayWidthPt, displayHeightPt);
            fz_append_string(ctx, contentBuf, cmd);
        } else {
            // No rotation - simple scale and position
            char cmd[128];
            snprintf(cmd, sizeof(cmd),
                     "%.4f 0 0 %.4f %.4f %.4f cm\n",
                     displayWidthPt, displayHeightPt, posX, pdfY);
            fz_append_string(ctx, contentBuf, cmd);
        }
        
        // Draw the image
        char doCmd[32];
        snprintf(doCmd, sizeof(doCmd), "/%s Do\n", imgName);
        fz_append_string(ctx, contentBuf, doCmd);
        
        fz_append_string(ctx, contentBuf, "Q\n");  // Restore graphics state
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] Added image" << imageIndex 
                 << "at (" << posX << "," << pdfY << ")"
                 << "size" << displayWidthPt << "x" << displayHeightPt
                 << "rotation" << img->rotation;
        #endif
    }
    fz_always(ctx) {
        // Clean up any resources that weren't transferred to the document
        if (imgBuf) fz_drop_buffer(ctx, imgBuf);
        if (fzImage) fz_drop_image(ctx, fzImage);
    }
    fz_catch(ctx) {
        qWarning() << "[MuPdfExporter] Failed to add image:" << fz_caught_message(ctx);
        return false;
    }
    
    return true;
}

// ============================================================================
// Highlight Annotations
// ============================================================================

QVector<QRectF> MuPdfExporter::highlightRectsToPdfSpace(const QVector<QRectF>& pageRects,
                                                        qreal pageHeightSn)
{
    QVector<QRectF> out;
    out.reserve(pageRects.size());
    for (const QRectF& r : pageRects) {
        // Same scale and flip as transformPoint(), applied to the corners: the
        // page-space bottom edge is the PDF-space lower edge.
        out.append(QRectF(r.left() * SN_TO_PDF_SCALE,
                          (pageHeightSn - r.bottom()) * SN_TO_PDF_SCALE,
                          r.width() * SN_TO_PDF_SCALE,
                          r.height() * SN_TO_PDF_SCALE));
    }
    return out;
}

/**
 * @brief A highlight's page-space rects, minus the degenerate ones.
 *
 * Filters on the same 0.1 threshold LinkObject::renderRegion() skips on, so
 * the quads, the /Rect and the appearance stream all describe the same lines.
 */
static QVector<QRectF> usableHighlightRects(const LinkObject* link)
{
    const QVector<QRectF> pageRects = link->regionRectsInPageSpace();
    QVector<QRectF> out;
    out.reserve(pageRects.size());
    for (const QRectF& r : pageRects) {
        if (r.width() < 0.1 || r.height() < 0.1) continue;
        out.append(r);
    }
    return out;
}

/**
 * @brief Build the /AP normal appearance for a highlight annotation.
 * @param snRects Page-space rects (96 DPI), already filtered
 * @param pdfRects The same rects in PDF space, index for index
 * @param bbox Annotation rectangle in PDF space; also the form's BBox
 * @param style Highlight style (never None here)
 * @param color Fill colour, drawn OPAQUE -- alpha lives on the annot's /CA
 * @param pageHeightSn Page height in SpeedyNote units, for the dot transform
 * @return Indirect Form XObject, or nullptr on failure
 *
 * Supplying our own appearance is the whole point of exporting highlights this
 * way: no viewer would draw an Underline as a tenth-of-line-height bar, and
 * none would draw a dotted underline at all. BBox equals /Rect and /Matrix is
 * left at identity, so the stream draws in page user space and the geometry
 * below is a direct transcription of LinkObject::renderRegion().
 */
static pdf_obj* buildHighlightAppearance(fz_context* ctx, pdf_document* outputDoc,
                                         const QVector<QRectF>& snRects,
                                         const QVector<QRectF>& pdfRects,
                                         const QRectF& bbox,
                                         HighlightRegion::Style style,
                                         const QColor& color,
                                         qreal pageHeightSn)
{
    pdf_obj* form = nullptr;
    fz_buffer* content = nullptr;
    
    fz_try(ctx) {
        content = fz_new_buffer(ctx, 256);
        
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f rg\n",
                 color.redF(), color.greenF(), color.blueF());
        fz_append_string(ctx, content, cmd);
        
        for (int i = 0; i < pdfRects.size(); ++i) {
            const QRectF& pr = pdfRects[i];
            
            // Derived from the unzoomed page-space rect and scaled once, the
            // way renderRegion() does it. Deriving it from the PDF-space rect
            // instead would clear the 1.5 floor at a different text size.
            const qreal tSn = HighlightRegion::underlineThickness(snRects[i]);
            const qreal tPt = tSn * SN_TO_PDF_SCALE;
            
            switch (style) {
                case HighlightRegion::Style::Cover:
                    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f re\n",
                             pr.x(), pr.y(), pr.width(), pr.height());
                    fz_append_string(ctx, content, cmd);
                    break;
                
                case HighlightRegion::Style::Underline:
                    // pr.y() is the lower edge in PDF space, so the bar sits
                    // on the baseline without any further flipping.
                    snprintf(cmd, sizeof(cmd), "%.4f %.4f %.4f %.4f re\n",
                             pr.x(), pr.y(), pr.width(), tPt);
                    fz_append_string(ctx, content, cmd);
                    break;
                
                case HighlightRegion::Style::DottedUnderline: {
                    // Placed in page space and transformed by the shared circle
                    // helper, which keeps this identical to renderRegion().
                    const QRectF& sr = snRects[i];
                    const qreal step = tSn * HighlightRegion::DOT_SPACING_FACTOR;
                    if (step <= 0.0) break;
                    
                    const qreal y = sr.bottom() - tSn * 0.5;
                    const qreal firstX = sr.left() + tSn * 0.5;
                    const qreal lastX = sr.right() - tSn * 0.5;
                    if (lastX < firstX) break;
                    
                    for (qreal x = firstX; x <= lastX; x += step) {
                        appendCircleToBuffer(ctx, content, QPointF(x, y),
                                             tSn * 0.5, pageHeightSn);
                    }
                    break;
                }
                
                case HighlightRegion::Style::None:
                    break;
            }
        }
        
        // One fill for every subpath, matching appendPolygonToBuffer's rule.
        fz_append_string(ctx, content, "f\n");
        
        form = pdf_new_dict(ctx, outputDoc, 4);
        pdf_dict_put(ctx, form, PDF_NAME(Type), PDF_NAME(XObject));
        pdf_dict_put(ctx, form, PDF_NAME(Subtype), PDF_NAME(Form));
        pdf_dict_put_int(ctx, form, PDF_NAME(FormType), 1);
        pdf_dict_put_rect(ctx, form, PDF_NAME(BBox),
                          fz_make_rect(bbox.x(), bbox.y(),
                                       bbox.x() + bbox.width(),
                                       bbox.y() + bbox.height()));
        
        // Must be indirect before pdf_update_stream(), as in importPageAsXObject().
        form = pdf_add_object(ctx, outputDoc, form);
        pdf_update_stream(ctx, outputDoc, form, content, 0);
    }
    fz_always(ctx) {
        if (content) fz_drop_buffer(ctx, content);
    }
    fz_catch(ctx) {
        qWarning() << "[MuPdfExporter] Failed to build highlight appearance:"
                   << fz_caught_message(ctx);
        return nullptr;
    }
    
    return form;
}

pdf_obj* MuPdfExporter::buildHighlightAnnots(const Page* page, qreal pageHeightSn)
{
    if (!page || !m_ctx || !m_outputDoc) {
        return nullptr;
    }
    
    std::vector<const LinkObject*> highlights;
    for (const auto& obj : page->objects) {
        const LinkObject* link = dynamic_cast<const LinkObject*>(obj.get());
        if (!link || !link->visible) continue;
        // An empty region is a standalone link icon, which has no geometry to
        // export; None is the legacy select-only value and draws nothing.
        if (link->region.isEmpty()) continue;
        if (link->region.style == HighlightRegion::Style::None) continue;
        if (!link->region.color.isValid()) continue;
        highlights.push_back(link);
    }
    if (highlights.empty()) {
        return nullptr;
    }
    
    // Ordered the way the page paints them, so where two highlights overlap the
    // later /Annots entry is the one the app draws on top.
    std::sort(highlights.begin(), highlights.end(),
              [](const LinkObject* a, const LinkObject* b) {
                  if (a->layerAffinity != b->layerAffinity) {
                      return a->layerAffinity < b->layerAffinity;
                  }
                  return a->zOrder < b->zOrder;
              });
    
    pdf_obj* annots = nullptr;
    
    fz_try(m_ctx) {
        annots = pdf_new_array(m_ctx, m_outputDoc, static_cast<int>(highlights.size()));
        
        for (const LinkObject* link : highlights) {
            const QVector<QRectF> snRects = usableHighlightRects(link);
            if (snRects.isEmpty()) continue;
            
            const QVector<QRectF> pdfRects = highlightRectsToPdfSpace(snRects, pageHeightSn);
            
            QRectF bbox;
            for (const QRectF& r : pdfRects) {
                bbox = bbox.isNull() ? r : bbox.united(r);
            }
            if (bbox.isNull()) continue;
            
            QColor color = link->region.color;
            if (m_options.darkenStrokes) {
                color = DarkModeUtils::darkenColorForExport(color);
            }
            
            pdf_obj* annot = pdf_new_dict(m_ctx, m_outputDoc, 12);
            pdf_dict_put(m_ctx, annot, PDF_NAME(Type), PDF_NAME(Annot));
            
            // Cover is a Highlight; both underline styles are Underline, which
            // is the closest standard type -- PDF has no dotted variant, so
            // DottedUnderline relies on the appearance stream below.
            pdf_dict_put(m_ctx, annot, PDF_NAME(Subtype),
                         link->region.style == HighlightRegion::Style::Cover
                             ? PDF_NAME(Highlight)
                             : PDF_NAME(Underline));
            
            pdf_dict_put_rect(m_ctx, annot, PDF_NAME(Rect),
                              fz_make_rect(bbox.x(), bbox.y(),
                                           bbox.x() + bbox.width(),
                                           bbox.y() + bbox.height()));
            
            // Quads describe the text that is marked, so they are the full line
            // rects for every style; only the appearance differs. Corner order
            // is upper-left, upper-right, lower-left, lower-right, which is
            // what producers and readers actually use.
            pdf_obj* quads = pdf_new_array(m_ctx, m_outputDoc, pdfRects.size() * 8);
            for (const QRectF& pr : pdfRects) {
                const qreal x0 = pr.x();
                const qreal x1 = pr.x() + pr.width();
                const qreal yLower = pr.y();
                const qreal yUpper = pr.y() + pr.height();
                pdf_array_push_real(m_ctx, quads, x0); pdf_array_push_real(m_ctx, quads, yUpper);
                pdf_array_push_real(m_ctx, quads, x1); pdf_array_push_real(m_ctx, quads, yUpper);
                pdf_array_push_real(m_ctx, quads, x0); pdf_array_push_real(m_ctx, quads, yLower);
                pdf_array_push_real(m_ctx, quads, x1); pdf_array_push_real(m_ctx, quads, yLower);
            }
            pdf_dict_put(m_ctx, annot, PDF_NAME(QuadPoints), quads);
            
            // Colour is stored opaque and the transparency goes on /CA, so it
            // cannot be applied twice by also baking it into the appearance,
            // and a reader can still adjust it.
            pdf_obj* colorArray = pdf_new_array(m_ctx, m_outputDoc, 3);
            pdf_array_push_real(m_ctx, colorArray, color.redF());
            pdf_array_push_real(m_ctx, colorArray, color.greenF());
            pdf_array_push_real(m_ctx, colorArray, color.blueF());
            pdf_dict_put(m_ctx, annot, PDF_NAME(C), colorArray);
            pdf_dict_put_real(m_ctx, annot, PDF_NAME(CA), color.alphaF());
            
            pdf_dict_put_int(m_ctx, annot, PDF_NAME(F), 4);  // Print
            pdf_dict_put_text_string(m_ctx, annot, PDF_NAME(T), "SpeedyNote");
            if (!link->id.isEmpty()) {
                pdf_dict_put_text_string(m_ctx, annot, pdf_new_name(m_ctx, "NM"),
                                         link->id.toUtf8().constData());
            }
            if (!link->description.isEmpty()) {
                pdf_dict_put_text_string(m_ctx, annot, PDF_NAME(Contents),
                                         link->description.toUtf8().constData());
            }
            
            // Deliberately no /BM /Multiply: the app composites with plain
            // source-over, and multiply would not match what the user saw.
            pdf_obj* form = buildHighlightAppearance(m_ctx, m_outputDoc, snRects, pdfRects,
                                                     bbox, link->region.style,
                                                     color, pageHeightSn);
            if (form) {
                pdf_obj* ap = pdf_new_dict(m_ctx, m_outputDoc, 1);
                pdf_dict_put(m_ctx, ap, PDF_NAME(N), form);
                pdf_dict_put(m_ctx, annot, PDF_NAME(AP), ap);
            }
            
            // Annotations are referenced indirectly; readers need that to treat
            // them as editable objects rather than inert page furniture.
            pdf_array_push(m_ctx, annots, pdf_add_object(m_ctx, m_outputDoc, annot));
        }
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to build highlight annotations:"
                   << fz_caught_message(m_ctx);
        return nullptr;
    }
    
    if (pdf_array_len(m_ctx, annots) == 0) {
        return nullptr;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Built" << pdf_array_len(m_ctx, annots)
             << "highlight annotation(s)";
    #endif
    return annots;
}

QByteArray MuPdfExporter::compressImage(const QImage& image, bool hasAlpha,
                                         const QSizeF& displaySizePt, int targetDpi)
{
    if (image.isNull()) {
        return QByteArray();
    }
    
    // Calculate if downsampling is needed
    // Display size is in PDF points (72 DPI)
    // Calculate the pixel size needed at target DPI
    QImage workImage = image;
    
    if (displaySizePt.width() > 0 && displaySizePt.height() > 0 && targetDpi > 0) {
        // Display size in inches
        qreal displayWidthInches = displaySizePt.width() / 72.0;
        qreal displayHeightInches = displaySizePt.height() / 72.0;
        
        // Required pixels at target DPI
        int requiredWidth = qRound(displayWidthInches * targetDpi);
        int requiredHeight = qRound(displayHeightInches * targetDpi);
        
        // Only downsample if image is larger than needed
        // (never upsample - that would increase file size without quality benefit)
        if (image.width() > requiredWidth || image.height() > requiredHeight) {
            // Calculate scale factor (maintain aspect ratio)
            qreal scaleX = static_cast<qreal>(requiredWidth) / image.width();
            qreal scaleY = static_cast<qreal>(requiredHeight) / image.height();
            qreal scale = qMin(scaleX, scaleY);
            
            int newWidth = qRound(image.width() * scale);
            int newHeight = qRound(image.height() * scale);
            
            // Ensure minimum size of 1x1
            newWidth = qMax(1, newWidth);
            newHeight = qMax(1, newHeight);
            
            #ifdef SPEEDYNOTE_DEBUG
            qDebug() << "[MuPdfExporter] Downsampling image from"
                     << image.width() << "x" << image.height()
                     << "to" << newWidth << "x" << newHeight
                     << "(target:" << targetDpi << "DPI)";
            #endif
            // Use smooth transformation for high quality downsampling
            workImage = image.scaled(newWidth, newHeight, 
                                     Qt::KeepAspectRatio, 
                                     Qt::SmoothTransformation);
        }
    }
    
    // Compress the (possibly downsampled) image
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    
    if (hasAlpha) {
        // PNG for images with transparency
        // PNG is lossless and preserves alpha channel
        if (!workImage.save(&buffer, "PNG")) {
            qWarning() << "[MuPdfExporter] Failed to compress image as PNG";
            return QByteArray();
        }
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] Compressed image as PNG:" 
                 << workImage.width() << "x" << workImage.height()
                 << "->" << result.size() << "bytes";
        #endif
    } else {
        // JPEG for opaque images (photos)
        // JPEG is lossy but much smaller for photographic content
        // Quality 85 is a good balance between size and quality
        QImage opaqueImage = workImage;
        
        // Convert to RGB if necessary (JPEG doesn't support alpha)
        if (opaqueImage.hasAlphaChannel()) {
            // Create an opaque version by compositing on white background
            QImage rgb(opaqueImage.size(), QImage::Format_RGB888);
            rgb.fill(Qt::white);
            QPainter painter(&rgb);
            painter.drawImage(0, 0, opaqueImage);
            painter.end();
            opaqueImage = rgb;
        } else if (opaqueImage.format() != QImage::Format_RGB888 &&
                   opaqueImage.format() != QImage::Format_RGB32) {
            opaqueImage = opaqueImage.convertToFormat(QImage::Format_RGB888);
        }
        
        if (!opaqueImage.save(&buffer, "JPEG", 85)) {
            qWarning() << "[MuPdfExporter] Failed to compress image as JPEG";
            return QByteArray();
        }
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] Compressed image as JPEG:" 
                 << workImage.width() << "x" << workImage.height()
                 << "->" << result.size() << "bytes";
        #endif
    }
    
    buffer.close();
    return result;
}

// ============================================================================
// Metadata and Outline (Phase 7 - TODO)
// ============================================================================

bool MuPdfExporter::writeMetadata()
{
    if (!m_outputDoc || !m_ctx) {
        return false;
    }
    
    fz_try(m_ctx) {
        // Get or create Info dictionary
        pdf_obj* info = pdf_dict_get(m_ctx, pdf_trailer(m_ctx, m_outputDoc), PDF_NAME(Info));
        if (!info) {
            info = pdf_new_dict(m_ctx, m_outputDoc, 8);
            pdf_dict_put_drop(m_ctx, pdf_trailer(m_ctx, m_outputDoc), PDF_NAME(Info), info);
        }
        
        // Copy metadata from source PDF if available
        if (m_sourceDoc) {
            // Copy Title
            char titleBuf[512] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc, 
                                   "info:Title", titleBuf, sizeof(titleBuf)) > 0 && titleBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Title), titleBuf);
            }
            
            // Copy Author
            char authorBuf[256] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc,
                                   "info:Author", authorBuf, sizeof(authorBuf)) > 0 && authorBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Author), authorBuf);
            }
            
            // Copy Subject
            char subjectBuf[512] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc,
                                   "info:Subject", subjectBuf, sizeof(subjectBuf)) > 0 && subjectBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Subject), subjectBuf);
            }
            
            // Copy Keywords
            char keywordsBuf[1024] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc,
                                   "info:Keywords", keywordsBuf, sizeof(keywordsBuf)) > 0 && keywordsBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Keywords), keywordsBuf);
            }
            
            // Copy Creator (original authoring application)
            char creatorBuf[256] = {0};
            if (fz_lookup_metadata(m_ctx, m_sourceDoc,
                                   "info:Creator", creatorBuf, sizeof(creatorBuf)) > 0 && creatorBuf[0]) {
                pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Creator), creatorBuf);
            }
        }
        
        // Add/override Producer with SpeedyNote attribution
        pdf_dict_put_text_string(m_ctx, info, PDF_NAME(Producer), "SpeedyNote 1.0");
        
        // Update ModDate to current time (PDF date format: D:YYYYMMDDHHmmSS)
        QDateTime now = QDateTime::currentDateTime();
        QString modDateStr = QStringLiteral("D:%1").arg(now.toString("yyyyMMddHHmmss"));
        QByteArray modDateUtf8 = modDateStr.toUtf8();
        pdf_dict_put_text_string(m_ctx, info, PDF_NAME(ModDate), modDateUtf8.constData());
        
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] Wrote metadata, ModDate:" << modDateStr;
        #endif
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to write metadata:" << fz_caught_message(m_ctx);
        return false;
    }
    
    return true;
}

bool MuPdfExporter::writeOutline(const QVector<int>& exportedPages)
{
    // OUT2: write bookmarks straight from Document::aggregatedOutline() - the exact
    // tree the on-screen outline panel shows. That tree already merges every
    // contributing PDF source (primary + imported), groups multiple sources under
    // titled roots, prunes with TOC coverage-span semantics, and floor-redirects
    // each surviving entry to a page that is actually present in the document.
    //
    // The previous per-source approach re-loaded raw fz_outlines and trimmed by an
    // EXACT page hit, so an imported page subset (whose bookmark targets rarely land
    // exactly on an imported page) lost all of its bookmarks. Consuming the
    // aggregated tree keeps export and panel identical and fixes that.
    if (!m_outputDoc || !m_ctx || !m_document) {
        return true;  // Not an error, just nothing to do
    }

    const QVector<PdfOutlineItem> outline = m_document->aggregatedOutline();
    if (outline.isEmpty()) {
        #ifdef SPEEDYNOTE_DEBUG
        qDebug() << "[MuPdfExporter] No aggregated outline, skipping";
        #endif
        return true;
    }

    // notebook page index -> export (output) page index for the pages being written.
    std::unordered_map<int, int> notebookToExport;
    notebookToExport.reserve(static_cast<size_t>(exportedPages.size()));
    for (int i = 0; i < exportedPages.size(); ++i) {
        notebookToExport[exportedPages[i]] = i;
    }

    // Resolve an aggregated item to its output page index. Items carry
    // (sourceId, targetPage) in the source's ORIGINAL page space; reachable
    // entries already point at a present page (aggregatedOutline floor-redirects),
    // while headers/roots and inert context entries have targetPage < 0 (or resolve
    // to a page outside this export) and become destination-less titles.
    Document* doc = m_document;
    auto exportIndexOf = [doc, &notebookToExport](const PdfOutlineItem& it) -> int {
        if (it.targetPage < 0) {
            return -1;
        }
        const int nb = doc->notebookPageIndexForSourcePage(it.sourceId, it.targetPage);
        if (nb < 0) {
            return -1;
        }
        auto f = notebookToExport.find(nb);
        return (f != notebookToExport.end()) ? f->second : -1;
    };

    fz_try(m_ctx) {
        pdf_obj* root = buildAggregatedOutline(m_ctx, m_outputDoc, outline, exportIndexOf);
        if (root) {
            pdf_obj* catalog = pdf_dict_get(m_ctx, pdf_trailer(m_ctx, m_outputDoc), PDF_NAME(Root));
            if (catalog) {
                pdf_dict_put(m_ctx, catalog, PDF_NAME(Outlines), root);
                pdf_dict_put(m_ctx, catalog, PDF_NAME(PageMode), PDF_NAME(UseOutlines));
            }
        }
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to write outline:" << fz_caught_message(m_ctx);
        return false;
    }

    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Wrote aggregated outline with" << outline.size() << "top-level item(s)";
    #endif
    return true;
}

/**
 * OUT2: recursively build the exported bookmark tree from a Document
 * aggregatedOutline() subtree. Every item in the passed vector is emitted (the
 * aggregated tree is already pruned/greyed upstream): items whose exportIndexOf()
 * resolves get a /Dest [page /Fit]; headers, source roots and inert context
 * entries are emitted as destination-less titles. Returns the container dict
 * (Type /Outlines) holding these items, or nullptr for an empty level.
 *
 * Outline items MUST be indirect objects (pdf_add_object): pdf_save_document
 * otherwise recurses infinitely in pdf_set_obj_parent while serializing the tree.
 */
static pdf_obj* buildAggregatedOutline(fz_context* ctx, pdf_document* outputDoc,
                                       const QVector<PdfOutlineItem>& items,
                                       const std::function<int(const PdfOutlineItem&)>& exportIndexOf)
{
    if (items.isEmpty() || !outputDoc || !ctx) {
        return nullptr;
    }

    struct Built { const PdfOutlineItem* item; pdf_obj* obj; };
    std::vector<Built> entries;
    entries.reserve(static_cast<size_t>(items.size()));

    for (const PdfOutlineItem& it : items) {
        // Recurse first so children exist before we wire First/Last/Count.
        pdf_obj* childContainer = it.children.isEmpty()
            ? nullptr
            : buildAggregatedOutline(ctx, outputDoc, it.children, exportIndexOf);

        pdf_obj* item = pdf_new_dict(ctx, outputDoc, 6);
        item = pdf_add_object(ctx, outputDoc, item);

        const QByteArray titleUtf8 = it.title.toUtf8();
        if (!titleUtf8.isEmpty()) {
            pdf_dict_put_text_string(ctx, item, PDF_NAME(Title), titleUtf8.constData());
        }

        const int exportIdx = exportIndexOf(it);
        if (exportIdx >= 0) {
            pdf_obj* dest = pdf_new_array(ctx, outputDoc, 2);
            pdf_obj* pageRef = pdf_lookup_page_obj(ctx, outputDoc, exportIdx);
            pdf_array_push(ctx, dest, pageRef);
            pdf_array_push(ctx, dest, PDF_NAME(Fit));
            pdf_dict_put_drop(ctx, item, PDF_NAME(Dest), dest);
        }

        if (childContainer) {
            pdf_obj* firstChild = pdf_dict_get(ctx, childContainer, PDF_NAME(First));
            pdf_obj* lastChild = pdf_dict_get(ctx, childContainer, PDF_NAME(Last));
            const int childCount = pdf_dict_get_int(ctx, childContainer, PDF_NAME(Count));
            if (firstChild && lastChild) {
                pdf_dict_put(ctx, item, PDF_NAME(First), firstChild);
                pdf_dict_put(ctx, item, PDF_NAME(Last), lastChild);
                for (pdf_obj* child = firstChild; child;
                     child = pdf_dict_get(ctx, child, PDF_NAME(Next))) {
                    pdf_dict_put(ctx, child, PDF_NAME(Parent), item);
                }
                // Count sign encodes open/closed; magnitude is the direct child count.
                const int count = it.isOpen ? childCount : -childCount;
                if (count != 0) {
                    pdf_dict_put_int(ctx, item, PDF_NAME(Count), count);
                }
            }
        }

        entries.push_back({&it, item});
    }

    // Link siblings with Prev/Next.
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i > 0) {
            pdf_dict_put(ctx, entries[i].obj, PDF_NAME(Prev), entries[i - 1].obj);
        }
        if (i + 1 < entries.size()) {
            pdf_dict_put(ctx, entries[i].obj, PDF_NAME(Next), entries[i + 1].obj);
        }
    }

    pdf_obj* container = pdf_new_dict(ctx, outputDoc, 4);
    container = pdf_add_object(ctx, outputDoc, container);
    pdf_dict_put(ctx, container, PDF_NAME(Type), PDF_NAME(Outlines));
    pdf_dict_put(ctx, container, PDF_NAME(First), entries.front().obj);
    pdf_dict_put(ctx, container, PDF_NAME(Last), entries.back().obj);
    pdf_dict_put_int(ctx, container, PDF_NAME(Count), static_cast<int>(entries.size()));
    for (const Built& b : entries) {
        pdf_dict_put(ctx, b.obj, PDF_NAME(Parent), container);
    }

    return container;
}

// ============================================================================
// Finalization
// ============================================================================

bool MuPdfExporter::saveDocument(const QString& outputPath)
{
    if (!m_outputDoc || !m_ctx) {
        return false;
    }
    
    QByteArray pathUtf8 = outputPath.toUtf8();
    
    fz_try(m_ctx) {
        // Write PDF with default options
        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 1;       // Compress streams
        opts.do_compress_images = 1; // Compress images
        opts.do_compress_fonts = 1;  // Compress fonts
        
        pdf_save_document(m_ctx, m_outputDoc, pathUtf8.constData(), &opts);
    }
    fz_catch(m_ctx) {
        qWarning() << "[MuPdfExporter] Failed to save document:" << fz_caught_message(m_ctx);
        return false;
    }
    
    #ifdef SPEEDYNOTE_DEBUG
    qDebug() << "[MuPdfExporter] Saved to" << outputPath;
    #endif
    return true;
}

#endif // SPEEDYNOTE_MUPDF_EXPORT

