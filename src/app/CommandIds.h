#pragma once

// Command IDs taken verbatim from the original's RT_MENU / RT_TOOLBAR
// resources (see resources/ui_spec.json). Only the ones referenced in code are
// named here; every other menu item still gets an action from the UI spec.
namespace Cmd {
// File
constexpr int FileNew = 57600, FileOpen = 57601, FileSave = 57603, FileSaveAs = 57604;
constexpr int FileMerge = 33115, FileMergeAsSelection = 33237;
constexpr int ImportObj = 33240, ExportP3dOld = 33215;
// New commands (not in the original), IDs >= 40000
constexpr int ExportObj = 40001, ShowTextures = 40002, OpenFromGameData = 40003, ImportFbx = 40004, ReloadTextures = 40005;
constexpr int FileOptions = 32914, FileRecent = 57616, FileExit = 57665;

// Edit
constexpr int Undo = 57643, Redo = 57644, Cut = 57635, Copy = 57634, Paste = 57637, Delete = 32933;
constexpr int SelectAll = 57642, Deselect = 32869, DeselectFaces = 33828, DeselectVertices = 33827;
constexpr int InvertSelection = 32870, SelectFacesFromPoints = 32945, SelectPointsFromFaces = 32946;
constexpr int GrowSelection = 33822, ShrinkSelection = 33823;
constexpr int SelectLocked = 32871, SelectHidden = 33098, UnselectTriangles = 33265;
constexpr int SelOpCopy = 33831, SelOpUnion = 33833, SelOpComplement = 33832, SelOpIntersection = 33834,
              SelOpComplementOfIntersection = 33835, SelOpRecall = 33830;
constexpr int CenterPin = 32874, UsePin = 33065;
constexpr int ModeObject = 32820, ModeFaces = 32817, ModeVertices = 32818, ModePin = 32819;

// Create
constexpr int CreatePoint = 32883, CreatePlane = 32885, CreateBox = 32886, CreateCircle = 33096,
              CreateSphere = 32887, CreateCylinder = 32888;
constexpr int CreateFace = 32941, CreateProxy = 33185, Extrude = 33085;

// View
constexpr int ViewFront = 32787, ViewTop = 32786, ViewLeft = 32785, ViewProjected = 32788, ViewRight = 33168,
              ViewBack = 33169, ViewBottom = 33170;
constexpr int ZoomIn = 33080, ZoomOut = 33081, ZoomDrawing = 33077, ZoomSelection = 33078;
constexpr int SolidFill = 32857, FaceCulling = 32858, Grid = 32859, LookAtPin = 32854, Refresh = 32850;
constexpr int HideSelection = 32865, Unhide = 32866, LockSelection = 32867, Unlock = 32868;

// Structure
constexpr int CheckFaces = 32921, CenterAll = 32944, FindComponents = 32925, FindNonClosed = 32930;
constexpr int TriangulateBack = 32923, TriangulateFwd = 32943, Squarize = 32924;

// Points
constexpr int SelectIsolated = 32922;
constexpr int TransformMove = 32903, TransformRotate = 32904, TransformScale = 32905;
constexpr int MergePoints = 32938, MergeNear = 33130, RemovePoints = 33267, PointProperties = 32891;

// Faces
constexpr int FaceReverse = 32935, FaceUncross = 32936, FaceRemove = 32937;
constexpr int DegeneratedCheck = 32948, DegeneratedRepair = 32947;
constexpr int FacesMoveTop = 33141, FacesMoveBottom = 33142, RecalcNormals = 32919, FaceProperties = 32890;

// Surfaces
constexpr int SharpEdge = 32940, SmoothEdge = 32939;
constexpr int UVSetAdd = 33519, UVSetDelete = 33520, UVSetChangeId = 33521;
constexpr int UVSetActivateFirst = 33523; // "Activate 0".."Activate 100" = 33523..33623

// Window
constexpr int FourViews = 32795, StatusBar = 59393, MainToolbar = 59392, ViewControlBar = 32784,
              EditToolbar = 32834, SourceControlBar = 33415;
constexpr int WinHistory = 32877, WinPosition = 32852, WinLods = 32835, WinMass = 33067, WinNamedProps = 32771,
              WinNamedSelections = 32860, WinSelOperators = 33157;

// Help
constexpr int About = 57664;

// Context menus
constexpr int LodNew = 32836, LodDuplicate = 32837, LodDelete = 32838, LodProperties = 32841;
constexpr int NselNew = 32861, NselDelete = 32862, NselDeleteAll = 32872, NselRename = 32863,
              NselRenameAll = 32873, NselDeleteEmpty = 33445, NselRedefine = 32864;
constexpr int NpropNew = 32772, NpropDelete = 32773, NpropEdit = 32774;
} // namespace Cmd
