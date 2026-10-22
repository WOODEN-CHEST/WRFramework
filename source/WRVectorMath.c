#include "WRVectorMath.h"
#include <stdbool.h>
#include <stddef.h>
#include "WRMath.h"


// Types.
/* The twelve 2x2 minors that the determinant and the inverse of a 4x4 matrix are built from, named as in raymath's
 * MatrixInvert: B00 to B05 come from columns 0 and 1, B06 to B11 from columns 2 and 3. */
typedef struct MatrixMinorsStruct
{
    double B00;
    double B01;
    double B02;
    double B03;
    double B04;
    double B05;
    double B06;
    double B07;
    double B08;
    double B09;
    double B10;
    double B11;
} MatrixMinors;


// Static functions.
static bool IsFiniteValue(double value)
{
    return !Math_IsNaNDouble(value) && !Math_IsInfinityDouble(value);
}

/* raymath's names: Acr is row r of column c, M[c * 4 + r]. */
static MatrixMinors GetMinors(const Matrix4Double* matrix)
{
    const double* M = matrix->M;
    double A00 = M[0], A01 = M[1], A02 = M[2], A03 = M[3];
    double A10 = M[4], A11 = M[5], A12 = M[6], A13 = M[7];
    double A20 = M[8], A21 = M[9], A22 = M[10], A23 = M[11];
    double A30 = M[12], A31 = M[13], A32 = M[14], A33 = M[15];
    return (MatrixMinors)
    {
        .B00 = (A00 * A11) - (A01 * A10),
        .B01 = (A00 * A12) - (A02 * A10),
        .B02 = (A00 * A13) - (A03 * A10),
        .B03 = (A01 * A12) - (A02 * A11),
        .B04 = (A01 * A13) - (A03 * A11),
        .B05 = (A02 * A13) - (A03 * A12),
        .B06 = (A20 * A31) - (A21 * A30),
        .B07 = (A20 * A32) - (A22 * A30),
        .B08 = (A20 * A33) - (A23 * A30),
        .B09 = (A21 * A32) - (A22 * A31),
        .B10 = (A21 * A33) - (A23 * A31),
        .B11 = (A22 * A33) - (A23 * A32)
    };
}

/* The Laplace expansion along the column pairs (0, 1) and (2, 3). */
static double GetDeterminantFromMinors(const MatrixMinors* minors)
{
    return (minors->B00 * minors->B11) - (minors->B01 * minors->B10) + (minors->B02 * minors->B09)
        + (minors->B03 * minors->B08) - (minors->B04 * minors->B07) + (minors->B05 * minors->B06);
}

/* The barycentric weights of a point from the dot products of a triangle's edges from its corner a (edge 0 to b, edge
 * 1 to c) and the point's offset from a (Ericson's, as raymath's Vector3Barycenter): the same for 2D and 3D. */
static bool TryGetBarycentricFromDots(double d00, double d01, double d11, double d20, double d21,
    Vector3Double* outWeights)
{
    double Denominator = (d00 * d11) - (d01 * d01);
    if ((Denominator == 0.0) || !IsFiniteValue(Denominator))
    {
        return false;
    }
    double V = ((d11 * d20) - (d01 * d21)) / Denominator;
    double W = ((d00 * d21) - (d01 * d20)) / Denominator;
    Vector3Double Weights = Vector3Double_Create(1.0 - (W + V), V, W);
    if (!Vector3Double_IsFinite(Weights))
    {
        return false;
    }
    *outWeights = Weights;
    return true;
}

/* Whether a box has its Min at most its Max on every axis: false for an inverted axis, and for a NaN bound, which
 * fails the comparisons. */
static bool IsBoxOrdered(BoundingBoxDouble box)
{
    return (box.Min.X <= box.Max.X) && (box.Min.Y <= box.Max.Y) && (box.Min.Z <= box.Max.Z);
}


// Public functions: 2D vectors.
Vector2Double Vector2Double_NormalizeOr(Vector2Double self, Vector2Double fallback)
{
    double Largest = Math_MaxDouble(Math_AbsDouble(self.X), Math_AbsDouble(self.Y));
    if (!Vector2Double_IsFinite(self) || !(Largest > 0.0))
    {
        return fallback;
    }

    // As in Vector3Double_NormalizeOr: divided by its larger component first, its length is between 1 and the square
    // root of 2, so it neither underflows nor overflows.
    Vector2Double Scaled = Vector2Double_Create(self.X / Largest, self.Y / Largest);
    double Length = Vector2Double_Length(Scaled);
    return Vector2Double_Create(Scaled.X / Length, Scaled.Y / Length);
}

Vector2Double Vector2Double_Project(Vector2Double self, Vector2Double onto)
{
    Vector2Double Unit = Vector2Double_NormalizeOr(onto, Vector2Double_Zero());
    return Vector2Double_Scale(Unit, Vector2Double_Dot(self, Unit));
}

Vector2Double Vector2Double_Reject(Vector2Double self, Vector2Double onto)
{
    return Vector2Double_Subtract(self, Vector2Double_Project(self, onto));
}

Vector2Double Vector2Double_Refract(Vector2Double self, Vector2Double normal, double ratio)
{
    // raymath's Vector2Refract term for term (GLSL's refract).
    double Cosine = Vector2Double_Dot(self, normal);
    double Root = 1.0 - (ratio * ratio * (1.0 - (Cosine * Cosine)));
    if (!(Root >= 0.0))
    {
        return Vector2Double_Zero();
    }
    double Along = (ratio * Cosine) + Math_SqrtDouble(Root);
    return Vector2Double_Create((ratio * self.X) - (Along * normal.X), (ratio * self.Y) - (Along * normal.Y));
}

Vector2Double Vector2Double_Rotate(Vector2Double self, double angle)
{
    double Cosine = Math_CosDouble(angle);
    double Sine = Math_SinDouble(angle);
    return Vector2Double_Create((self.X * Cosine) - (self.Y * Sine), (self.X * Sine) + (self.Y * Cosine));
}

double Vector2Double_Angle(Vector2Double a, Vector2Double b)
{
    Vector2Double UnitA = Vector2Double_NormalizeOr(a, Vector2Double_Zero());
    Vector2Double UnitB = Vector2Double_NormalizeOr(b, Vector2Double_Zero());
    // Math_ATan2Double takes (x, y): the dot product is the x, the cross product the y.
    return Math_ATan2Double(Vector2Double_Dot(UnitA, UnitB), Vector2Double_Cross(UnitA, UnitB));
}

Vector2Double Vector2Double_FromAngle(double angle)
{
    return Vector2Double_Create(Math_CosDouble(angle), Math_SinDouble(angle));
}

double Vector2Double_GetAngle(Vector2Double self)
{
    return Math_ATan2Double(self.X, self.Y);
}

Vector2Double Vector2Double_MoveTowards(Vector2Double self, Vector2Double target, double maxDistance)
{
    Vector2Double Offset = Vector2Double_Subtract(target, self);
    double Distance = Vector2Double_Length(Offset);
    if ((Distance == 0.0) || ((maxDistance >= 0.0) && (Distance <= maxDistance)))
    {
        return target;
    }
    return Vector2Double_AddScaled(self, Offset, maxDistance / Distance);
}

Vector2Double Vector2Double_ClampLength(Vector2Double self, double minimumLength, double maximumLength)
{
    double Length = Vector2Double_Length(self);
    if (!(Length > 0.0))
    {
        return self;
    }
    if (Length < minimumLength)
    {
        return Vector2Double_Scale(self, minimumLength / Length);
    }
    if (Length > maximumLength)
    {
        return Vector2Double_Scale(self, maximumLength / Length);
    }
    return self;
}

bool Vector2Double_TryGetBarycentric(Vector2Double point, Vector2Double a, Vector2Double b, Vector2Double c,
    Vector3Double* outWeights)
{
    // Ericson's, as raymath's Vector3Barycenter, from the corner a.
    Vector2Double Edge0 = Vector2Double_Subtract(b, a);
    Vector2Double Edge1 = Vector2Double_Subtract(c, a);
    Vector2Double ToPoint = Vector2Double_Subtract(point, a);
    double D00 = Vector2Double_Dot(Edge0, Edge0);
    double D01 = Vector2Double_Dot(Edge0, Edge1);
    double D11 = Vector2Double_Dot(Edge1, Edge1);
    double D20 = Vector2Double_Dot(ToPoint, Edge0);
    double D21 = Vector2Double_Dot(ToPoint, Edge1);
    return TryGetBarycentricFromDots(D00, D01, D11, D20, D21, outWeights);
}


// Public functions: 3D vectors.
Vector3Double Vector3Double_NormalizeOr(Vector3Double self, Vector3Double fallback)
{
    double Largest = Math_MaxDouble(Math_AbsDouble(self.X), Math_MaxDouble(Math_AbsDouble(self.Y),
        Math_AbsDouble(self.Z)));
    if (!Vector3Double_IsFinite(self) || !(Largest > 0.0))
    {
        return fallback;
    }

    // Divided by its largest component first, its largest component is exactly 1 and its length between 1 and the
    // square root of 3, so the length neither underflows nor overflows whatever the vector's size.
    Vector3Double Scaled = Vector3Double_Create(self.X / Largest, self.Y / Largest, self.Z / Largest);
    double Length = Vector3Double_Length(Scaled);
    return Vector3Double_Create(Scaled.X / Length, Scaled.Y / Length, Scaled.Z / Length);
}

Vector3Double Vector3Double_Perpendicular(Vector3Double self)
{
    // raymath's choice: the axis with the smallest component, the first of equals.
    double Least = Math_AbsDouble(self.X);
    Vector3Double Axis = Vector3Double_Create(1.0, 0.0, 0.0);
    if (Math_AbsDouble(self.Y) < Least)
    {
        Least = Math_AbsDouble(self.Y);
        Axis = Vector3Double_Create(0.0, 1.0, 0.0);
    }
    if (Math_AbsDouble(self.Z) < Least)
    {
        Axis = Vector3Double_Create(0.0, 0.0, 1.0);
    }
    return Vector3Double_Cross(self, Axis);
}

Vector3Double Vector3Double_Project(Vector3Double self, Vector3Double onto)
{
    Vector3Double Unit = Vector3Double_NormalizeOr(onto, Vector3Double_Zero());
    return Vector3Double_Scale(Unit, Vector3Double_Dot(self, Unit));
}

Vector3Double Vector3Double_Reject(Vector3Double self, Vector3Double onto)
{
    return Vector3Double_Subtract(self, Vector3Double_Project(self, onto));
}

Vector3Double Vector3Double_Refract(Vector3Double self, Vector3Double normal, double ratio)
{
    // raymath's Vector3Refract term for term (GLSL's refract).
    double Cosine = Vector3Double_Dot(self, normal);
    double Root = 1.0 - (ratio * ratio * (1.0 - (Cosine * Cosine)));
    if (!(Root >= 0.0))
    {
        return Vector3Double_Zero();
    }
    double Along = (ratio * Cosine) + Math_SqrtDouble(Root);
    return Vector3Double_Create((ratio * self.X) - (Along * normal.X), (ratio * self.Y) - (Along * normal.Y),
        (ratio * self.Z) - (Along * normal.Z));
}

Vector3Double Vector3Double_RotateByAxisAngle(Vector3Double self, Vector3Double axis, double angle)
{
    // The Euler-Rodrigues formula, as raymath's: with w the unit axis times sin(angle / 2) and a = cos(angle / 2), the
    // turned vector is v + 2a (w x v) + 2 (w x (w x v)).
    double Half = angle * 0.5;
    Vector3Double W = Vector3Double_Scale(Vector3Double_NormalizeOr(axis, Vector3Double_Zero()), Math_SinDouble(Half));
    double A = Math_CosDouble(Half);
    Vector3Double WCrossV = Vector3Double_Cross(W, self);
    Vector3Double WCrossWCrossV = Vector3Double_Cross(W, WCrossV);
    return Vector3Double_Add(Vector3Double_Add(self, Vector3Double_Scale(WCrossV, 2.0 * A)),
        Vector3Double_Scale(WCrossWCrossV, 2.0));
}

double Vector3Double_Angle(Vector3Double a, Vector3Double b)
{
    Vector3Double UnitA = Vector3Double_NormalizeOr(a, Vector3Double_Zero());
    Vector3Double UnitB = Vector3Double_NormalizeOr(b, Vector3Double_Zero());
    // Math_ATan2Double takes (x, y): the dot product is the x, the cross product's length the y.
    return Math_ATan2Double(Vector3Double_Dot(UnitA, UnitB), Vector3Double_Length(Vector3Double_Cross(UnitA, UnitB)));
}

Vector3Double Vector3Double_MoveTowards(Vector3Double self, Vector3Double target, double maxDistance)
{
    Vector3Double Offset = Vector3Double_Subtract(target, self);
    double Distance = Vector3Double_Length(Offset);
    if ((Distance == 0.0) || ((maxDistance >= 0.0) && (Distance <= maxDistance)))
    {
        return target;
    }
    return Vector3Double_AddScaled(self, Offset, maxDistance / Distance);
}

Vector3Double Vector3Double_ClampLength(Vector3Double self, double minimumLength, double maximumLength)
{
    double Length = Vector3Double_Length(self);
    if (!(Length > 0.0))
    {
        return self;
    }
    if (Length < minimumLength)
    {
        return Vector3Double_Scale(self, minimumLength / Length);
    }
    if (Length > maximumLength)
    {
        return Vector3Double_Scale(self, maximumLength / Length);
    }
    return self;
}

bool Vector3Double_TryGetBarycentric(Vector3Double point, Vector3Double a, Vector3Double b, Vector3Double c,
    Vector3Double* outWeights)
{
    Vector3Double Edge0 = Vector3Double_Subtract(b, a);
    Vector3Double Edge1 = Vector3Double_Subtract(c, a);
    Vector3Double ToPoint = Vector3Double_Subtract(point, a);
    return TryGetBarycentricFromDots(Vector3Double_Dot(Edge0, Edge0), Vector3Double_Dot(Edge0, Edge1),
        Vector3Double_Dot(Edge1, Edge1), Vector3Double_Dot(ToPoint, Edge0), Vector3Double_Dot(ToPoint, Edge1), outWeights);
}


// Public functions: matrices.
Matrix4Double Matrix4Double_FromRotationX(double angle)
{
    Matrix4Double Result = Matrix4Double_Identity();
    double Cosine = Math_CosDouble(angle);
    double Sine = Math_SinDouble(angle);
    Result.M[5] = Cosine;
    Result.M[6] = Sine;
    Result.M[9] = -Sine;
    Result.M[10] = Cosine;
    return Result;
}

Matrix4Double Matrix4Double_FromRotationY(double angle)
{
    Matrix4Double Result = Matrix4Double_Identity();
    double Cosine = Math_CosDouble(angle);
    double Sine = Math_SinDouble(angle);
    Result.M[0] = Cosine;
    Result.M[2] = -Sine;
    Result.M[8] = Sine;
    Result.M[10] = Cosine;
    return Result;
}

Matrix4Double Matrix4Double_FromRotationZ(double angle)
{
    Matrix4Double Result = Matrix4Double_Identity();
    double Cosine = Math_CosDouble(angle);
    double Sine = Math_SinDouble(angle);
    Result.M[0] = Cosine;
    Result.M[1] = Sine;
    Result.M[4] = -Sine;
    Result.M[5] = Cosine;
    return Result;
}

Matrix4Double Matrix4Double_FromRotationXYZ(Vector3Double angles)
{
    // raymath's MatrixRotateXYZ term for term, which builds it from the negated angles.
    double CosZ = Math_CosDouble(-angles.Z);
    double SinZ = Math_SinDouble(-angles.Z);
    double CosY = Math_CosDouble(-angles.Y);
    double SinY = Math_SinDouble(-angles.Y);
    double CosX = Math_CosDouble(-angles.X);
    double SinX = Math_SinDouble(-angles.X);

    Matrix4Double Result = Matrix4Double_Identity();
    Result.M[0] = CosZ * CosY;
    Result.M[1] = (CosZ * SinY * SinX) - (SinZ * CosX);
    Result.M[2] = (CosZ * SinY * CosX) + (SinZ * SinX);
    Result.M[4] = SinZ * CosY;
    Result.M[5] = (SinZ * SinY * SinX) + (CosZ * CosX);
    Result.M[6] = (SinZ * SinY * CosX) - (CosZ * SinX);
    Result.M[8] = -SinY;
    Result.M[9] = CosY * SinX;
    Result.M[10] = CosY * CosX;
    return Result;
}

double Matrix4Double_GetDeterminant(const Matrix4Double* self)
{
    MatrixMinors Minors = GetMinors(self);
    return GetDeterminantFromMinors(&Minors);
}

bool Matrix4Double_TryInvert(const Matrix4Double* self, Matrix4Double* outInverse)
{
    MatrixMinors Minors = GetMinors(self);
    double Determinant = GetDeterminantFromMinors(&Minors);
    if ((Determinant == 0.0) || !IsFiniteValue(Determinant))
    {
        return false;
    }
    double InverseDeterminant = 1.0 / Determinant;

    // raymath's MatrixInvert in double: the adjugate (the cofactors' transpose) over the determinant.
    const double* M = self->M;
    double A00 = M[0], A01 = M[1], A02 = M[2], A03 = M[3];
    double A10 = M[4], A11 = M[5], A12 = M[6], A13 = M[7];
    double A20 = M[8], A21 = M[9], A22 = M[10], A23 = M[11];
    double A30 = M[12], A31 = M[13], A32 = M[14], A33 = M[15];
    Matrix4Double Inverse;
    Inverse.M[0] = ((A11 * Minors.B11) - (A12 * Minors.B10) + (A13 * Minors.B09)) * InverseDeterminant;
    Inverse.M[1] = ((-A01 * Minors.B11) + (A02 * Minors.B10) - (A03 * Minors.B09)) * InverseDeterminant;
    Inverse.M[2] = ((A31 * Minors.B05) - (A32 * Minors.B04) + (A33 * Minors.B03)) * InverseDeterminant;
    Inverse.M[3] = ((-A21 * Minors.B05) + (A22 * Minors.B04) - (A23 * Minors.B03)) * InverseDeterminant;
    Inverse.M[4] = ((-A10 * Minors.B11) + (A12 * Minors.B08) - (A13 * Minors.B07)) * InverseDeterminant;
    Inverse.M[5] = ((A00 * Minors.B11) - (A02 * Minors.B08) + (A03 * Minors.B07)) * InverseDeterminant;
    Inverse.M[6] = ((-A30 * Minors.B05) + (A32 * Minors.B02) - (A33 * Minors.B01)) * InverseDeterminant;
    Inverse.M[7] = ((A20 * Minors.B05) - (A22 * Minors.B02) + (A23 * Minors.B01)) * InverseDeterminant;
    Inverse.M[8] = ((A10 * Minors.B10) - (A11 * Minors.B08) + (A13 * Minors.B06)) * InverseDeterminant;
    Inverse.M[9] = ((-A00 * Minors.B10) + (A01 * Minors.B08) - (A03 * Minors.B06)) * InverseDeterminant;
    Inverse.M[10] = ((A30 * Minors.B04) - (A31 * Minors.B02) + (A33 * Minors.B00)) * InverseDeterminant;
    Inverse.M[11] = ((-A20 * Minors.B04) + (A21 * Minors.B02) - (A23 * Minors.B00)) * InverseDeterminant;
    Inverse.M[12] = ((-A10 * Minors.B09) + (A11 * Minors.B07) - (A12 * Minors.B06)) * InverseDeterminant;
    Inverse.M[13] = ((A00 * Minors.B09) - (A01 * Minors.B07) + (A02 * Minors.B06)) * InverseDeterminant;
    Inverse.M[14] = ((-A30 * Minors.B03) + (A31 * Minors.B01) - (A32 * Minors.B00)) * InverseDeterminant;
    Inverse.M[15] = ((A20 * Minors.B03) - (A21 * Minors.B01) + (A22 * Minors.B00)) * InverseDeterminant;
    if (!Matrix4Double_IsFinite(&Inverse))
    {
        return false;
    }
    *outInverse = Inverse;
    return true;
}

bool Matrix4Double_TryInvertAffine(const Matrix4Double* self, Matrix4Double* outInverse)
{
    // The linear part's columns a, b and c: its inverse's rows are b x c, c x a and a x b, each over the determinant
    // a . (b x c).
    const double* M = self->M;
    Vector3Double ColumnX = Vector3Double_Create(M[0], M[1], M[2]);
    Vector3Double ColumnY = Vector3Double_Create(M[4], M[5], M[6]);
    Vector3Double ColumnZ = Vector3Double_Create(M[8], M[9], M[10]);
    Vector3Double Rows[3] =
    {
        Vector3Double_Cross(ColumnY, ColumnZ),
        Vector3Double_Cross(ColumnZ, ColumnX),
        Vector3Double_Cross(ColumnX, ColumnY)
    };
    double Determinant = Vector3Double_Dot(ColumnX, Rows[0]);
    if ((Determinant == 0.0) || !IsFiniteValue(Determinant))
    {
        return false;
    }
    double InverseDeterminant = 1.0 / Determinant;

    Vector3Double Translation = Matrix4Double_GetTranslation(self);
    Matrix4Double Inverse = Matrix4Double_Identity();
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        Vector3Double InverseRow = Vector3Double_Scale(Rows[Row], InverseDeterminant);
        Inverse.M[Row] = InverseRow.X;
        Inverse.M[4U + Row] = InverseRow.Y;
        Inverse.M[8U + Row] = InverseRow.Z;
        Inverse.M[12U + Row] = -Vector3Double_Dot(InverseRow, Translation);
    }
    if (!Matrix4Double_IsFinite(&Inverse))
    {
        return false;
    }
    *outInverse = Inverse;
    return true;
}


// Public functions: boxes.
BoundingBoxDouble BoundingBoxDouble_Transform(BoundingBoxDouble self, const Matrix4Double* matrix)
{
    const double* M = matrix->M;
    const double Mins[3] = { self.Min.X, self.Min.Y, self.Min.Z };
    const double Maxes[3] = { self.Max.X, self.Max.Y, self.Max.Z };
    double OutMins[3];
    double OutMaxes[3];
    for (size_t Row = 0U; Row < 3U; Row++)
    {
        double Lows[3];
        double Highs[3];
        for (size_t Column = 0U; Column < 3U; Column++)
        {
            double Entry = M[(Column * 4U) + Row];
            if (Entry == 0.0)
            {
                // This axis does not reach that output axis. Multiplying would give 0 times infinity for an infinite
                // bound; for finite bounds the zero's sign is all that differs.
                Lows[Column] = 0.0;
                Highs[Column] = 0.0;
                continue;
            }
            double AtMin = Entry * Mins[Column];
            double AtMax = Entry * Maxes[Column];
            Lows[Column] = Math_MinDouble(AtMin, AtMax);
            Highs[Column] = Math_MaxDouble(AtMin, AtMax);
        }
        // Summed as Matrix4Double_TransformPoint sums a corner's products, so every corner falls inside.
        OutMins[Row] = ((Lows[0] + Lows[1]) + Lows[2]) + M[12U + Row];
        OutMaxes[Row] = ((Highs[0] + Highs[1]) + Highs[2]) + M[12U + Row];
    }
    return (BoundingBoxDouble)
    {
        .Min = Vector3Double_Create(OutMins[0], OutMins[1], OutMins[2]),
        .Max = Vector3Double_Create(OutMaxes[0], OutMaxes[1], OutMaxes[2])
    };
}

bool BoundingBoxDouble_IntersectsRay(BoundingBoxDouble self, Vector3Double origin, Vector3Double direction,
    double maxDistance, double* outDistance)
{
    if (!Vector3Double_IsFinite(origin) || !Vector3Double_IsFinite(direction) || !(maxDistance >= 0.0)
        || !IsBoxOrdered(self))
    {
        return false;
    }

    const double Origins[3] = { origin.X, origin.Y, origin.Z };
    const double Directions[3] = { direction.X, direction.Y, direction.Z };
    const double Mins[3] = { self.Min.X, self.Min.Y, self.Min.Z };
    const double Maxes[3] = { self.Max.X, self.Max.Y, self.Max.Z };
    double Near = 0.0;
    double Far = maxDistance;
    for (size_t Axis = 0U; Axis < 3U; Axis++)
    {
        if (Directions[Axis] == 0.0)
        {
            // Parallel to this axis's slab: inside it along the whole ray, or nowhere.
            if ((Origins[Axis] < Mins[Axis]) || (Origins[Axis] > Maxes[Axis]))
            {
                return false;
            }
            continue;
        }

        // Divided, not multiplied by a reciprocal: a face through the origin gives 0, never 0 times infinity.
        double Entry = (Mins[Axis] - Origins[Axis]) / Directions[Axis];
        double Exit = (Maxes[Axis] - Origins[Axis]) / Directions[Axis];
        if (Entry > Exit)
        {
            double Swapped = Entry;
            Entry = Exit;
            Exit = Swapped;
        }
        Near = Math_MaxDouble(Near, Entry);
        Far = Math_MinDouble(Far, Exit);
        if (Near > Far)
        {
            return false;
        }
    }

    if (outDistance != NULL)
    {
        *outDistance = Near;
    }
    return true;
}
