#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <numeric>
#include <strings.h>
#include <assert.h>
#include <sys/types.h>
#include <stdexcept>
#include <filesystem>
#include <iomanip>  // std::setprecision()

#include <dirent.h>

#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/io.hpp>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/adapted/c_array.hpp>

BOOST_GEOMETRY_REGISTER_C_ARRAY_CS(cs::cartesian)

typedef boost::geometry::model::polygon<boost::geometry::model::d2::point_xy<double> > Polygon;


using namespace std;

/*=======================================================================
STATIC EVALUATION PARAMETERS
=======================================================================*/

// easy and hard evaluation level
enum DIFFICULTY{EASY=0, HARD=1};

const int32_t N_TESTIMAGES = 6203; // 6203 is validation set, 27661 is train set;

const int32_t MIN_3D_N_POINTS = 10;
const double MAX_3D_DIST[2] = {15, 25};
const double MIN_2D_AREA[2] = {1600, 500};
const double MAX_2D_OCC = 2;
// evaluation metrics: image, ground or 3D
enum METRIC{IMAGE=0, GROUND=1, BOX3D=2};

// evaluated object classes
enum CLASSES{CAR=0, PEDESTRIAN=1, CYCLIST=2};
const int NUM_CLASS = 3;

// parameters varying per class
vector<string> CLASS_NAMES;
vector<string> CLASS_NAMES_CAP;
// the minimum overlap required for 2D evaluation on the image/ground plane and 3D evaluation
//const double MIN_OVERLAP[3][3] = {{0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}};
const double MIN_OVERLAP[3][3] = {{0.3, 0.5, 0.7}, {0.3, 0.5, 0.7}, {0.3, 0.5, 0.7}};

// no. of recall steps that should be evaluated (discretized)
const double N_SAMPLE_PTS = 41;

/* BEGIN STAT LOGGING */
map<size_t, string> gtdetidx_to_frame_map;
/* END STAT LOGGING */

// initialize class names
void initGlobals () {
  CLASS_NAMES.push_back("car");
  CLASS_NAMES.push_back("pedestrian");
  CLASS_NAMES.push_back("cyclist");
  CLASS_NAMES_CAP.push_back("Car");
  CLASS_NAMES_CAP.push_back("Pedestrian");
  CLASS_NAMES_CAP.push_back("Cyclist");
}

/*=======================================================================
DATA TYPES FOR EVALUATION
=======================================================================*/

// holding data needed for precision-recall and precision-aos
struct tPrData {
  vector<double> v;           // detection score for computing score thresholds
  double         similarity;  // orientation similarity
  int32_t        tp;          // true positives
  int32_t        fp;          // false positives
  int32_t        fn;          // false negatives
  tPrData () :
    similarity(0), tp(0), fp(0), fn(0) {}
};

// holding bounding boxes for ground truth and detections
struct tBox {
  string  type;     // object type as car, pedestrian or cyclist,...
  double   x1;      // left corner
  double   y1;      // top corner
  double   x2;      // right corner
  double   y2;      // bottom corner
  double   alpha;   // image orientation
  tBox (string type, double x1,double y1,double x2,double y2,double alpha) :
    type(type),x1(x1),y1(y1),x2(x2),y2(y2),alpha(alpha) {}
};

// holding ground truth data
struct tGroundtruth {
  tBox    box;        // object type, box, orientation
  int32_t  truncation; // truncation 0..1
  int32_t occlusion;  // occlusion 0,1,2,3 (non, partly, partly, fully)
  int32_t num_points_3d;
  double ry;
  double  t1, t2, t3;
  double h, w, l;
  tGroundtruth () :
    box(tBox("invalild",-1,-1,-1,-1,-10)),truncation(-1),occlusion(-1) {}
  tGroundtruth (tBox box,int32_t truncation,int32_t occlusion) :
    box(box),truncation(truncation),occlusion(occlusion) {}
  tGroundtruth (string type,double x1,double y1,double x2,double y2,double alpha,int32_t truncation,int32_t occlusion) :
    box(tBox(type,x1,y1,x2,y2,alpha)),truncation(truncation),occlusion(occlusion) {}
};

// holding detection data
struct tDetection {
  tBox    box;    // object type, box, orientation
  double  thresh; // detection score
  double  ry;
  double  t1, t2, t3;
  double  h, w, l;
  tDetection ():
    box(tBox("invalid",-1,-1,-1,-1,-10)),thresh(-1000) {}
  tDetection (tBox box,double thresh) :
    box(box),thresh(thresh) {}
  tDetection (string type,double x1,double y1,double x2,double y2,double alpha,double thresh) :
    box(tBox(type,x1,y1,x2,y2,alpha)),thresh(thresh) {}
};


/*=======================================================================
FUNCTIONS TO LOAD DETECTION AND GROUND TRUTH DATA ONCE, SAVE RESULTS
=======================================================================*/
vector<tDetection> loadDetection(string file_name) {

  vector<tDetection> detections;
  FILE *fp = fopen(file_name.c_str(),"r");
  if (!fp)
    throw invalid_argument("cannot read detection file " + file_name);
  while (!feof(fp)) {
    tDetection d;
    int trash;
    char str[255];
    if (fscanf(fp, "%s %d %d %d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
                   str, &trash, &trash, &trash, &d.box.alpha, &d.box.x1, &d.box.y1,
                   //&d.box.x2, &d.box.y2, &d.h, &d.w, &d.l, &d.t1, &d.t2, &d.t3, //old
                   &d.box.x2, &d.box.y2, &d.l, &d.h, &d.w, &d.t1, &d.t2, &d.t3, //new
                   &d.ry, &d.thresh)==17) {
      d.box.type = str;
      detections.push_back(d);
    }
  }

  fclose(fp);
  return detections;
}

vector<tGroundtruth> loadGroundtruth(string file_name) {
  vector<tGroundtruth> groundtruth;
  FILE *fp = fopen(file_name.c_str(),"r");
  if (!fp)
    throw invalid_argument("cannot read ground truth file " + file_name);
  while (!feof(fp)) {
    tGroundtruth g;
    int trash;
    char str[255];
    if (fscanf(fp, "%s %d %d %d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %d %d",
                   str, &g.truncation, &g.occlusion, &g.num_points_3d,
                   &g.box.alpha, &g.box.x1, &g.box.y1, &g.box.x2, &g.box.y2,
                   //&g.h, &g.w, &g.l, &g.t1, &g.t2, &g.t3, &g.ry, &trash)==17) { // old
                    &g.l, &g.h, &g.w, &g.t1, &g.t2, &g.t3, &g.ry, &trash)==17) { // new
      g.box.type = str;
      groundtruth.push_back(g);
    }
  }
  fclose(fp);
  return groundtruth;
}


vector<string> list_dir(const string path) {
  struct dirent *entry;
  DIR *dir = opendir(path.c_str());
  vector<string> entries;

  if (dir == NULL) {
    return entries;
  }
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
      entries.push_back(entry->d_name);
  }
  closedir(dir);
  return entries;
}

/*=======================================================================
EVALUATION HELPER FUNCTIONS
=======================================================================*/

// criterion defines whether the overlap is computed with respect to both areas (ground truth and detection)
// or with respect to box a or b (detection and "dontcare" areas)
inline double imageBoxOverlap(tBox a, tBox b, int32_t criterion=-1){

  // overlap is invalid in the beginning
  double o = -1;

  // get overlapping area
  double x1 = max(a.x1, b.x1);
  double y1 = max(a.y1, b.y1);
  double x2 = min(a.x2, b.x2);
  double y2 = min(a.y2, b.y2);

  // compute width and height of overlapping area
  double w = x2-x1;
  double h = y2-y1;

  // set invalid entries to 0 overlap
  if(w<=0 || h<=0)
    return 0;

  // get overlapping areas
  double inter = w*h;
  double a_area = (a.x2-a.x1) * (a.y2-a.y1);
  double b_area = (b.x2-b.x1) * (b.y2-b.y1);

  // intersection over union overlap depending on users choice
  if(criterion==-1)     // union
    o = inter / (a_area+b_area-inter);
  else if(criterion==0) // bbox_a
    o = inter / a_area;
  else if(criterion==1) // bbox_b
    o = inter / b_area;

  // overlap
  return o;
}

inline double imageBoxOverlap(tDetection a, tGroundtruth b, int32_t criterion=-1){
  return imageBoxOverlap(a.box, b.box, criterion);
}

// compute polygon of an oriented bounding box
template <typename T>
Polygon toPolygon(const T& g) {
    using namespace boost::numeric::ublas;
    using namespace boost::geometry;
    matrix<double> mref(2, 2);
    mref(0, 0) = cos(g.ry); mref(0, 1) = sin(g.ry);
    mref(1, 0) = -sin(g.ry); mref(1, 1) = cos(g.ry);

    static int count = 0;
    matrix<double> corners(2, 4);
    double data[] = {g.l / 2, g.l / 2, -g.l / 2, -g.l / 2,
                     g.w / 2, -g.w / 2, -g.w / 2, g.w / 2};
    std::copy(data, data + 8, corners.data().begin());
    matrix<double> gc = prod(mref, corners);
    for (int i = 0; i < 4; ++i) {
        gc(0, i) += g.t1;
        gc(1, i) += g.t3;
    }

    double points[][2] = {{gc(0, 0), gc(1, 0)},{gc(0, 1), gc(1, 1)},{gc(0, 2), gc(1, 2)},{gc(0, 3), gc(1, 3)},{gc(0, 0), gc(1, 0)}};
    Polygon poly;
    append(poly, points);
    return poly;
}

// measure overlap between bird's eye view bounding boxes, parametrized by (ry, l, w, tx, tz)
inline double groundBoxOverlap(tDetection d, tGroundtruth g, int32_t criterion = -1) {
    using namespace boost::geometry;
    Polygon gp = toPolygon(g);
    Polygon dp = toPolygon(d);

    std::vector<Polygon> in, un;
    intersection(gp, dp, in);
    union_(gp, dp, un);

    double inter_area = in.empty() ? 0 : area(in.front());
    double union_area = area(un.front());
    double o;
    if(criterion==-1)     // union
        o = inter_area / union_area;
    else if(criterion==0) // bbox_a
        o = inter_area / area(dp);
    else if(criterion==1) // bbox_b
        o = inter_area / area(gp);

    return o;
}

// measure overlap between 3D bounding boxes, parametrized by (ry, h, w, l, tx, ty, tz)
inline double box3DOverlap(tDetection d, tGroundtruth g, int32_t criterion = -1) {
    using namespace boost::geometry;
    Polygon gp = toPolygon(g);
    Polygon dp = toPolygon(d);

    std::vector<Polygon> in, un;
    intersection(gp, dp, in);
    union_(gp, dp, un);

    double ymax = min(d.t2, g.t2);
    double ymin = max(d.t2 - d.h, g.t2 - g.h);

    double inter_area = in.empty() ? 0 : area(in.front());
    double inter_vol = inter_area * max(0.0, ymax - ymin);

    double det_vol = d.h * d.l * d.w;
    double gt_vol = g.h * g.l * g.w;

    double o;
    if(criterion==-1)     // union
        o = inter_vol / (det_vol + gt_vol - inter_vol);
    else if(criterion==0) // bbox_a
        o = inter_vol / det_vol;
    else if(criterion==1) // bbox_b
        o = inter_vol / gt_vol;

    return o;
}

vector<double> getThresholds(vector<double> &v, double n_groundtruth){

  // holds scores needed to compute N_SAMPLE_PTS recall values
  vector<double> t;

  // sort scores in descending order
  // (highest score is assumed to give best/most confident detections)
  sort(v.begin(), v.end(), greater<double>());

  // get scores for linearly spaced recall
  double current_recall = 0;
  for(int32_t i=0; i<v.size(); i++){

    // check if right-hand-side recall with respect to current recall is close than left-hand-side one
    // in this case, skip the current detection score
    double l_recall, r_recall, recall;
    l_recall = (double)(i+1)/n_groundtruth;
    if(i<(v.size()-1))
      r_recall = (double)(i+2)/n_groundtruth;
    else
      r_recall = l_recall;

    if( (r_recall-current_recall) < (current_recall-l_recall) && i<(v.size()-1))
      continue;

    // left recall is the best approximation, so use this and goto next recall step for approximation
    recall = l_recall;

    // the next recall step was reached
    t.push_back(v[i]);
    current_recall += 1.0/(N_SAMPLE_PTS-1.0);
  }
  return t;
}

void cleanData(
    CLASSES current_class, 
    const vector<tGroundtruth> &gt, 
    const vector<tDetection> &det, 
    vector<int32_t> &ignored_gt, 
    vector<tGroundtruth> &dc, 
    vector<int32_t> &ignored_det, 
    int32_t &n_gt, 
    DIFFICULTY difficulty, bool depth
  ) {

  // extract ground truth bounding boxes for current evaluation class
  for(int32_t i=0;i<gt.size(); i++){

    // neighboring classes are ignored ("van" for "car" and "person_sitting" for "pedestrian")
    // (lower/upper cases are ignored)
    int32_t valid_class;

    // all classes without a neighboring class
    if(!strcasecmp(gt[i].box.type.c_str(), CLASS_NAMES[current_class].c_str()))
      valid_class = 1;

    // classes with a neighboring class
    else if(!strcasecmp(CLASS_NAMES[current_class].c_str(), "Pedestrian") && !strcasecmp("Person_sitting", gt[i].box.type.c_str()))
      valid_class = 0;
    else if(!strcasecmp(CLASS_NAMES[current_class].c_str(), "Car") && !strcasecmp("Van", gt[i].box.type.c_str()))
      valid_class = 0;

    // classes not used for evaluation
    else
      valid_class = -1;
    bool ignore = false;
    bool invalid = false;
    // 3D groundtruth filter criteria
    if (depth) {
      if (gt[i].num_points_3d < 0)
        invalid = true;
      if (gt[i].num_points_3d < MIN_3D_N_POINTS)
        ignore = true;
      if (gt[i].t1 * gt[i].t1 + gt[i].t3 * gt[i].t3 > MAX_3D_DIST[difficulty] * MAX_3D_DIST[difficulty])
        ignore = true;
    } else {
      double height = gt[i].box.y2 - gt[i].box.y1;
      double width = gt[i].box.x2 - gt[i].box.x1;
      double area = width * height;
      if (gt[i].box.x1 < 0)
        invalid = true;
      if (area < MIN_2D_AREA[difficulty])
        ignore = true;
      if (gt[i].occlusion > MAX_2D_OCC)
        ignore = true;
    }
    // set ignored vector for ground truth
    // current class and not ignored (total no. of ground truth is detected for recall denominator)
    if(invalid)
      ignored_gt.push_back(-1);
    else if(valid_class==1 && !ignore){
      ignored_gt.push_back(0);
      n_gt++;
    }
    else
      ignored_gt.push_back(1);
  }

  // extract dontcare areas
  for(int32_t i=0;i<gt.size(); i++) {
    // cout << "Num dc " << gt[i].box.type.c_str() << endl;
    // cout << "Add dc? " << !strcasecmp("DontCare", gt[i].box.type.c_str()) << endl;
    if(!strcasecmp("DontCare", gt[i].box.type.c_str())) {
      dc.push_back(gt[i]);
    }
  }

  // extract detections bounding boxes of the current class
  for(int32_t i=0;i<det.size(); i++){

    // neighboring classes are not evaluated
    int32_t valid_class;
    if(!strcasecmp(det[i].box.type.c_str(), CLASS_NAMES[current_class].c_str()))
      valid_class = 1;
    else
      valid_class = -1;

    bool ignore = false;
    if (depth) {
      if (det[i].t1 * det[i].t1 + det[i].t3 * det[i].t3 > MAX_3D_DIST[difficulty] * MAX_3D_DIST[difficulty])
        ignore = true;
    } else {
      double height = det[i].box.y2 - det[i].box.y1;
      double width = det[i].box.x2 - det[i].box.x1;
      double area = width * height;
      if (area < MIN_2D_AREA[difficulty])
        ignore = true;
    }
    // set ignored vector for detections
    if(ignore) {
      ignored_det.push_back(1);
    } else if(valid_class==1) {
      ignored_det.push_back(0);
    } else {
      cout << "You should not be seeing this valid_class " << valid_class << '\n';
      ignored_det.push_back(-1);
    }
  }
}

void write_stat_result(
  string outfilepre, const vector< vector<tGroundtruth> > &groundtruth, 
  const vector< vector<tDetection> > &detection, 
  const vector<vector<vector<int32_t> > > &tp_indices, 
  const vector<vector<vector<int32_t> > > &fp_indices, 
  const vector<vector<vector<int32_t> > > &fn_indices, int32_t thres_idx, int32_t frame
) {
  /*
  num frames
    num thresholds
      detection indices (variable)
  */
  ofstream outfile;
  string outfilename = outfilepre + "tp.txt";
  outfile.open(outfilename);
  for (auto tp_gt_index : tp_indices[frame][thres_idx]) {
    outfile << setprecision(9)  << groundtruth[frame][tp_gt_index].t1 << " "
                                << groundtruth[frame][tp_gt_index].t2 << " "
                                << groundtruth[frame][tp_gt_index].t3 << " "
                                << groundtruth[frame][tp_gt_index].h << " "
                                << groundtruth[frame][tp_gt_index].w << " "
                                << groundtruth[frame][tp_gt_index].l << " "
                                << groundtruth[frame][tp_gt_index].ry << '\n';
  }
  outfile.close();

  outfilename = outfilepre + "fp.txt";
  outfile.open(outfilename);
  for (auto fp_det_index : fp_indices[frame][thres_idx]) {
    outfile << setprecision(9)  << detection[frame][fp_det_index].t1 << " "
                                << detection[frame][fp_det_index].t2 << " "
                                << detection[frame][fp_det_index].t3 << " "
                                << detection[frame][fp_det_index].h << " "
                                << detection[frame][fp_det_index].w << " "
                                << detection[frame][fp_det_index].l << " "
                                << detection[frame][fp_det_index].ry << '\n';
  }
  outfile.close();

  outfilename = outfilepre + "fn.txt";
  outfile.open(outfilename);
  for (auto fn_gt_index : fn_indices[frame][thres_idx]) {
    outfile << setprecision(9)  << groundtruth[frame][fn_gt_index].t1 << " "
                                << groundtruth[frame][fn_gt_index].t2 << " "
                                << groundtruth[frame][fn_gt_index].t3 << " "
                                << groundtruth[frame][fn_gt_index].h << " "
                                << groundtruth[frame][fn_gt_index].w << " "
                                << groundtruth[frame][fn_gt_index].l << " "
                                << groundtruth[frame][fn_gt_index].ry << '\n';
  }
  outfile.close();
}

// default version
tPrData computeStatistics(CLASSES current_class, const vector<tGroundtruth> &gt,
        const vector<tDetection> &det, const vector<tGroundtruth> &dc,
        const vector<int32_t> &ignored_gt, const vector<int32_t>  &ignored_det,
        bool compute_fp, double (*boxoverlap)(tDetection, tGroundtruth, int32_t),
        METRIC metric, bool compute_aos=false, double thresh=0, bool debug=false){

  tPrData stat = tPrData();
  const double NO_DETECTION = -10000000;
  vector<double> delta;            // holds angular difference for TPs (needed for AOS evaluation)
  vector<bool> assigned_detection; // holds wether a detection was assigned to a valid or ignored ground truth
  assigned_detection.assign(det.size(), false);
  vector<bool> ignored_threshold;
  ignored_threshold.assign(det.size(), false); // holds detections with a threshold lower than thresh if FP are computed

  // detections with a low score are ignored for computing precision (needs FP)
  if(compute_fp)
    for(int32_t i=0; i<det.size(); i++)
      if(det[i].thresh<thresh)
        ignored_threshold[i] = true;

  // evaluate all ground truth boxes
  for(int32_t i=0; i<gt.size(); i++){

    // this ground truth is not of the current or a neighboring class and therefore ignored
    if(ignored_gt[i]==-1)
      continue;

    /*=======================================================================
    find candidates (overlap with ground truth > 0.5) (logical len(det))
    =======================================================================*/
    int32_t det_idx          = -1;
    double valid_detection = NO_DETECTION;
    double max_overlap     = 0;

    // search for a possible detection
    bool assigned_ignored_det = false;
    for(int32_t j=0; j<det.size(); j++){

      // detections not of the current class, already assigned or with a low threshold are ignored
      if(ignored_det[j]==-1)
        continue;
      if(assigned_detection[j])
        continue;
      if(ignored_threshold[j])
        continue;
      // find the maximum score for the candidates and get idx of respective detection
      double overlap = boxoverlap(det[j], gt[i], -1);

      // for computing recall thresholds, the candidate with highest score is considered
      if(!compute_fp && overlap>MIN_OVERLAP[metric][current_class] && det[j].thresh>valid_detection){
        det_idx         = j;
        valid_detection = det[j].thresh;
      }

      // for computing pr curve values, the candidate with the greatest overlap is considered
      // if the greatest overlap is an ignored detection, the overlapping detection is used
      else if(compute_fp && overlap>MIN_OVERLAP[metric][current_class] && (overlap>max_overlap || assigned_ignored_det) && ignored_det[j]==0){
        max_overlap     = overlap;
        det_idx         = j;
        valid_detection = 1;
        assigned_ignored_det = false;
      }
      else if(compute_fp && overlap>MIN_OVERLAP[metric][current_class] && valid_detection==NO_DETECTION && ignored_det[j]==1){
        det_idx              = j;
        valid_detection      = 1;
        assigned_ignored_det = true;
      }
    }

    /*=======================================================================
    compute TP, FP and FN
    =======================================================================*/

    // nothing was assigned to this valid ground truth
    if(valid_detection==NO_DETECTION && ignored_gt[i]==0) {
      stat.fn++;
    }

    // only evaluate valid ground truth <=> detection assignments
    else if(valid_detection!=NO_DETECTION && (ignored_gt[i]==1 || ignored_det[det_idx]==1))
      assigned_detection[det_idx] = true;

    // found a valid true positive
    else if(valid_detection!=NO_DETECTION){

      // write highest score to threshold vector
      stat.tp++;
      stat.v.push_back(det[det_idx].thresh);

      // compute angular difference of detection and ground truth if valid detection orientation was provided
      if(compute_aos)
        delta.push_back(gt[i].box.alpha - det[det_idx].box.alpha);

      // clean up
      assigned_detection[det_idx] = true;
    }
  }

  // if FP are requested, consider stuff area
  if(compute_fp){

    // count fp
    for(int32_t i=0; i<det.size(); i++){

      // count false positives if required (height smaller than required is ignored (ignored_det==1)
      if(!(assigned_detection[i] || ignored_det[i]==-1 || ignored_det[i]==1 || ignored_threshold[i]))
        stat.fp++;
    }

    // do not consider detections overlapping with stuff area
    int32_t nstuff = 0;
    for(int32_t i=0; i<dc.size(); i++){
      for(int32_t j=0; j<det.size(); j++){

        // detections not of the current class, already assigned, with a low threshold or a low minimum height are ignored
        if(assigned_detection[j])
          continue;
        if(ignored_det[j]==-1 || ignored_det[j]==1)
          continue;
        if(ignored_threshold[j])
          continue;

        // compute overlap and assign to stuff area, if overlap exceeds class specific value
        double overlap = boxoverlap(det[j], dc[i], 0);
        if(overlap>MIN_OVERLAP[metric][current_class]){
          assigned_detection[j] = true;
          nstuff++;
        }
      }
    }

    // FP = no. of all not to ground truth assigned detections - detections assigned to stuff areas
    stat.fp -= nstuff;

    // if all orientation values are valid, the AOS is computed
    if(compute_aos){
      vector<double> tmp;

      // FP have a similarity of 0, for all TP compute AOS
      tmp.assign(stat.fp, 0);
      for(int32_t i=0; i<delta.size(); i++)
        tmp.push_back((1.0+cos(delta[i]))/2.0);

      // be sure, that all orientation deltas are computed
      assert(tmp.size()==stat.fp+stat.tp);
      assert(delta.size()==stat.tp);

      // get the mean orientation similarity for this image
      if(stat.tp>0 || stat.fp>0)
        stat.similarity = accumulate(tmp.begin(), tmp.end(), 0.0);

      // there was neither a FP nor a TP, so the similarity is ignored in the evaluation
      else
        stat.similarity = -1;
    }
  }

  return stat;
}

// custom version
tPrData computeStatistics(CLASSES current_class, const vector<tGroundtruth> &gt,
        const vector<tDetection> &det, const vector<tGroundtruth> &dc,
        const vector<int32_t> &ignored_gt, const vector<int32_t>  &ignored_det,
        bool compute_fp, double (*boxoverlap)(tDetection, tGroundtruth, int32_t),
        METRIC metric, 
        vector<int32_t> &tp_indices, vector<int32_t> &fp_indices, vector<int32_t> &fn_indices,
        bool compute_aos=false, double thresh=0, bool debug=false){
  tPrData stat = tPrData();
  const double NO_DETECTION = -10000000;
  vector<double> delta;            // holds angular difference for TPs (needed for AOS evaluation)
  vector<bool> assigned_detection; // holds wether a detection was assigned to a valid or ignored ground truth
  assigned_detection.assign(det.size(), false);
  vector<bool> ignored_threshold;
  ignored_threshold.assign(det.size(), false); // holds detections with a threshold lower than thresh if FP are computed

  // detections with a low score are ignored for computing precision (needs FP)
  if(compute_fp)
    for(int32_t i=0; i<det.size(); i++)
      if(det[i].thresh<thresh)
        ignored_threshold[i] = true;

  // evaluate all ground truth boxes
  for(int32_t i=0; i<gt.size(); i++){

    // this ground truth is not of the current or a neighboring class and therefore ignored
    if(ignored_gt[i]==-1)
      continue;

    /*=======================================================================
    find candidates (overlap with ground truth > 0.5) (logical len(det))
    =======================================================================*/
    int32_t det_idx          = -1;
    double valid_detection = NO_DETECTION;
    double max_overlap     = 0;

    // search for a possible detection
    bool assigned_ignored_det = false;
    for(int32_t j=0; j<det.size(); j++){

      // detections not of the current class, already assigned or with a low threshold are ignored
      if(ignored_det[j]==-1)
        continue;
      if(assigned_detection[j])
        continue;
      if(ignored_threshold[j])
        continue;
      // find the maximum score for the candidates and get idx of respective detection
      double overlap = boxoverlap(det[j], gt[i], -1);

      // for computing recall thresholds, the candidate with highest score is considered
      if(!compute_fp && overlap>MIN_OVERLAP[metric][current_class] && det[j].thresh>valid_detection){
        det_idx         = j;
        valid_detection = det[j].thresh;
      }

      // for computing pr curve values, the candidate with the greatest overlap is considered
      // if the greatest overlap is an ignored detection, the overlapping detection is used
      else if(compute_fp && overlap>MIN_OVERLAP[metric][current_class] && (overlap>max_overlap || assigned_ignored_det) && ignored_det[j]==0){
        max_overlap     = overlap;
        det_idx         = j;
        valid_detection = 1;
        assigned_ignored_det = false;
      }
      else if(compute_fp && overlap>MIN_OVERLAP[metric][current_class] && valid_detection==NO_DETECTION && ignored_det[j]==1){
        det_idx              = j;
        valid_detection      = 1;
        assigned_ignored_det = true;
      }
    }

    /*=======================================================================
    compute TP, FP and FN
    =======================================================================*/

    // nothing was assigned to this valid ground truth
    if(valid_detection==NO_DETECTION && ignored_gt[i]==0) {
      stat.fn++;
      fn_indices.push_back(i);
    }

    // only evaluate valid ground truth <=> detection assignments
    else if(valid_detection!=NO_DETECTION && (ignored_gt[i]==1 || ignored_det[det_idx]==1))
      assigned_detection[det_idx] = true;

    // found a valid true positive
    else if(valid_detection!=NO_DETECTION){

      // write highest score to threshold vector
      stat.tp++;
      stat.v.push_back(det[det_idx].thresh);
      tp_indices.push_back(i);

      // compute angular difference of detection and ground truth if valid detection orientation was provided
      if(compute_aos)
        delta.push_back(gt[i].box.alpha - det[det_idx].box.alpha);

      // clean up
      assigned_detection[det_idx] = true;
    }
  }

  // if FP are requested, consider stuff area
  if(compute_fp){

    // count fp
    for(int32_t i=0; i<det.size(); i++){

      // count false positives if required (height smaller than required is ignored (ignored_det==1)
      if(!(assigned_detection[i] || ignored_det[i]==-1 || ignored_det[i]==1 || ignored_threshold[i]))
        stat.fp++;
        fp_indices.push_back(i);
    }

    // do not consider detections overlapping with stuff area
    int32_t nstuff = 0;
    for(int32_t i=0; i<dc.size(); i++){
      for(int32_t j=0; j<det.size(); j++){

        // detections not of the current class, already assigned, with a low threshold or a low minimum height are ignored
        if(assigned_detection[j])
          continue;
        if(ignored_det[j]==-1 || ignored_det[j]==1)
          continue;
        if(ignored_threshold[j])
          continue;

        // compute overlap and assign to stuff area, if overlap exceeds class specific value
        double overlap = boxoverlap(det[j], dc[i], 0);
        if(overlap>MIN_OVERLAP[metric][current_class]){
          assigned_detection[j] = true;
          nstuff++;
          auto fp_indices_it = find(fp_indices.begin(), fp_indices.end(), j);
          fp_indices.erase(fp_indices_it);
        }
      }
    }

    // FP = no. of all not to ground truth assigned detections - detections assigned to stuff areas
    stat.fp -= nstuff;

    // if all orientation values are valid, the AOS is computed
    if(compute_aos){
      vector<double> tmp;

      // FP have a similarity of 0, for all TP compute AOS
      tmp.assign(stat.fp, 0);
      for(int32_t i=0; i<delta.size(); i++)
        tmp.push_back((1.0+cos(delta[i]))/2.0);

      // be sure, that all orientation deltas are computed
      assert(tmp.size()==stat.fp+stat.tp);
      assert(delta.size()==stat.tp);

      // get the mean orientation similarity for this image
      if(stat.tp>0 || stat.fp>0)
        stat.similarity = accumulate(tmp.begin(), tmp.end(), 0.0);

      // there was neither a FP nor a TP, so the similarity is ignored in the evaluation
      else
        stat.similarity = -1;
    }
  }

  return stat;
}

/*=======================================================================
EVALUATE CLASS-WISE
=======================================================================*/

// default version
bool eval_class(CLASSES current_class,
        const vector< vector<tGroundtruth> > &groundtruth,
        const vector< vector<tDetection> > &detections, bool compute_aos,
        double (*boxoverlap)(tDetection, tGroundtruth, int32_t),
        vector<double> &precision,
        METRIC metric, DIFFICULTY difficulty, bool depth) {
  assert(groundtruth.size() == detections.size());

  // init
  int32_t n_gt=0;                                     // total no. of gt (denominator of recall)
  vector<double> v, thresholds;                       // detection scores, evaluated for recall discretization
  vector< vector<int32_t> > ignored_gt, ignored_det;  // index of ignored gt detection for current class
  vector< vector<tGroundtruth> > dontcare;            // index of dontcare areas, included in ground truth

  // for all test images do
  for (int32_t i=0; i<groundtruth.size(); i++){

    // holds ignored ground truth, ignored detections and dontcare areas for current frame
    vector<int32_t> i_gt, i_det;
    vector<tGroundtruth> dc;
    CLASSES tmp_1 = (CLASSES)1;
    // only evaluate objects of current class and ignore occluded, truncated objects
    cleanData(tmp_1, groundtruth[i], detections[i], i_gt, dc, i_det, n_gt, difficulty, depth);
    ignored_gt.push_back(i_gt);
    ignored_det.push_back(i_det);
    dontcare.push_back(dc);

    // compute statistics to get recall values
    tPrData pr_tmp = tPrData();
    pr_tmp = computeStatistics(current_class, groundtruth[i], detections[i], dc, i_gt, i_det, false, boxoverlap, metric);

    // add detection scores to vector over all images
    for(int32_t j=0; j<pr_tmp.v.size(); j++)
      v.push_back(pr_tmp.v[j]);
  }
  // cout << "ignored gt " << ignored_gt.size() << " ignored det " << ignored_det.size() << " dontcare " << dontcare.size() << endl;
  // get scores that must be evaluated for recall discretization
  thresholds = getThresholds(v, n_gt);

  // compute TP,FP,FN for relevant scores
  vector<tPrData> pr;
  pr.assign(thresholds.size(),tPrData());
  for (int32_t i=0; i<groundtruth.size(); i++){

    // for all scores/recall thresholds do:
    for(int32_t t=0; t<thresholds.size(); t++){
      tPrData tmp = tPrData();
      tmp = computeStatistics(current_class, groundtruth[i], detections[i], dontcare[i],
                              ignored_gt[i], ignored_det[i], true, boxoverlap, metric,
                              compute_aos, thresholds[t], t==38);

      // add no. of TP, FP, FN, AOS for current frame to total evaluation for current threshold
      pr[t].tp += tmp.tp;
      pr[t].fp += tmp.fp;
      pr[t].fn += tmp.fn;
      if(tmp.similarity!=-1)
        pr[t].similarity += tmp.similarity;
    }
  }

  // compute recall, precision and AOS
  precision.assign(N_SAMPLE_PTS, 0);
  double r=0;
  for (int32_t i=0; i<thresholds.size(); i++){
    r = pr[i].tp/(double)(pr[i].tp + pr[i].fn);
    precision[i] = pr[i].tp/(double)(pr[i].tp + pr[i].fp);
  }

  // filter precision and AOS using max_{i..end}(precision)
  for (int32_t i=0; i<thresholds.size(); i++){
    precision[i] = *max_element(precision.begin()+i, precision.end());
  }

  return true;
}

// custom version
bool eval_class(CLASSES current_class,
        const vector< vector<tGroundtruth> > &groundtruth,
        const vector< vector<tDetection> > &detections, bool compute_aos,
        double (*boxoverlap)(tDetection, tGroundtruth, int32_t),
        vector<double> &precision,
        vector<double> &recall,
        METRIC metric, DIFFICULTY difficulty, bool depth, bool write_to_file=false) {
  assert(groundtruth.size() == detections.size());

  // init
  int32_t n_gt=0;                                     // total no. of gt (denominator of recall)
  vector<double> v, thresholds;                       // detection scores, evaluated for recall discretization
  vector< vector<int32_t> > ignored_gt, ignored_det;  // index of ignored gt detection for current class
  vector< vector<tGroundtruth> > dontcare;            // index of dontcare areas, included in ground truth

  vector<int32_t> tmp_tp_indices;
  vector<int32_t> tmp_fp_indices;
  vector<int32_t> tmp_fn_indices;

  // for all test images do
  for (int32_t i=0; i<groundtruth.size(); i++){

    // holds ignored ground truth, ignored detections and dontcare areas for current frame
    vector<int32_t> i_gt, i_det;
    vector<tGroundtruth> dc;
    CLASSES tmp_1 = (CLASSES)1;
    // only evaluate objects of current class and ignore occluded, truncated objects
    cleanData(tmp_1, groundtruth[i], detections[i], i_gt, dc, i_det, n_gt, difficulty, depth);
    ignored_gt.push_back(i_gt);
    ignored_det.push_back(i_det);
    dontcare.push_back(dc);

    // compute statistics to get recall values
    tPrData pr_tmp = tPrData();
    pr_tmp = computeStatistics(current_class, groundtruth[i], detections[i], dc, i_gt, i_det, false, boxoverlap, metric, tmp_tp_indices, tmp_fp_indices, tmp_fn_indices);
    // pr_tmp = computeStatistics(current_class, groundtruth[i], detections[i], dc, i_gt, i_det, true, boxoverlap, metric); // compute recall for this
    //Empty temp stat vectors
    tmp_tp_indices.clear();
    tmp_fp_indices.clear();
    tmp_fn_indices.clear();

    // add detection scores to vector over all images
    for(int32_t j=0; j<pr_tmp.v.size(); j++)
      v.push_back(pr_tmp.v[j]);
  }
  // cout << "ignored gt " << ignored_gt.size() << " ignored det " << ignored_det.size() << " dontcare " << dontcare.size() << endl;
  // get scores that must be evaluated for recall discretization
  thresholds = getThresholds(v, n_gt);
  // cout << "Thesholds: ";
  // for (auto threshold : thresholds) {
  //   cout << threshold << " ";
  // }
  // cout << "\n";
  const size_t N_FRAMES = groundtruth.size();
  const size_t N_THRESHOLDS = thresholds.size();
  vector<vector<vector<int32_t> > > tp_indices(N_FRAMES, vector<vector<int32_t> >(N_THRESHOLDS, vector<int32_t>()) );
  vector<vector<vector<int32_t> > > fp_indices(N_FRAMES, vector<vector<int32_t> >(N_THRESHOLDS, vector<int32_t>()) );
  vector<vector<vector<int32_t> > > fn_indices(N_FRAMES, vector<vector<int32_t> >(N_THRESHOLDS, vector<int32_t>()) );

  // compute TP,FP,FN for relevant scores
  vector<tPrData> pr;
  pr.assign(thresholds.size(),tPrData());
  for (int32_t i=0; i<groundtruth.size(); i++){
    // for all scores/recall thresholds do:
    for(int32_t t=0; t<thresholds.size(); t++){
      tPrData tmp = tPrData();
      tmp = computeStatistics(current_class, groundtruth[i], detections[i], dontcare[i],
                              ignored_gt[i], ignored_det[i], true, boxoverlap, metric,
                              tp_indices[i][t], fp_indices[i][t], fn_indices[i][t],
                              compute_aos, thresholds[t], t==38);
      // add no. of TP, FP, FN, AOS for current frame to total evaluation for current threshold
      pr[t].tp += tmp.tp;
      pr[t].fp += tmp.fp;
      pr[t].fn += tmp.fn;
      // if (tp_indices[i][t].size()>0) 
      //   cout << "tp indices " << t << " parent func size " << tp_indices[i][t].size() << '\n';
      if(tmp.similarity!=-1)
        pr[t].similarity += tmp.similarity;
    }
  }

  // compute recall, precision and AOS
  precision.assign(N_THRESHOLDS, 0); // potential bug, if num thresholds < N_SAMPLE_PTS
  recall.assign(N_THRESHOLDS, 0);
  // precision.assign(N_SAMPLE_PTS, 0); // potential bug, if num thresholds < N_SAMPLE_PTS
  // recall.assign(N_SAMPLE_PTS, 0);
  double r=0;
  for (int32_t i=0; i<thresholds.size(); i++){
    r = pr[i].tp/(double)(pr[i].tp + pr[i].fn);
    precision[i] = pr[i].tp/(double)(pr[i].tp + pr[i].fp);
    recall[i] = r;
  }

  // Save predictions from threshold with highest precision
  const size_t VIS_THRES_INDEX = size_t(N_THRESHOLDS/2);

  if (write_to_file) {
    for (size_t idx = 0; idx<groundtruth.size(); ++idx) {
      // cout << "Saving evaluations with for frame " << to_string(frame) << " with max threshold " << to_string(max_thres_index) << '\n';

      //Map idx to frame
      string imageset_frame = gtdetidx_to_frame_map[idx];
      // if (imageset_frame=="002308") {
      //   cout << "idx " << idx << " frame " << imageset_frame << '\n';
      // }

      string outfilepre = "/robodata/arthurz/Benchmarks/jrdb_toolkit/detection_eval/eval_pr_out/coda2jrdbfullrangeepoch22/" + imageset_frame + "/";
      filesystem::create_directory(outfilepre);

      write_stat_result(outfilepre, groundtruth, detections, tp_indices, fp_indices, fn_indices, VIS_THRES_INDEX, idx);
    }
    cout << "Done writing tp, fp, fn results to files\n";
  }

  // Don't apply precision filter because it amplifies precision, affecting f1 score
  // filter precision and AOS using max_{i..end}(precision)
  // for (int32_t i=0; i<N_THRESHOLDS; i++){
  //   precision[i] = *max_element(precision.begin()+i, precision.end());
  //   // recall[i] = *max_element(recall.begin()+i, recall.end());
  // }

  return true;
}

// default version
void write_result(ofstream& outfile, string exp_name, vector<double> &precisions) {
  double ap = accumulate(precisions.begin() + 1, precisions.end(), 0.0) / (N_SAMPLE_PTS - 1);
  outfile << exp_name << "," << ap ;
  for (const double& prec : precisions) {
    outfile << ',' << prec;
  }
  outfile << endl;
}

// custom version
void write_result(ofstream& outfile, string exp_name, vector<double> &precisions, vector<double> &recalls) {
  // fixes tp average computation
  double ap = accumulate(precisions.begin(), precisions.end(), 0.0) / (precisions.size());
  double ar = accumulate(recalls.begin(), recalls.end(), 0.0) / (recalls.size());
  double af1 = (double)(2 * ap * ar) / (ap + ar);
  outfile << exp_name << "," << ap << "," << ar << "," << af1;
  for (const double& prec : precisions) {
    outfile << ',' << prec;
  }
  for (const double& rec : recalls) {
    outfile << "," << rec;
  }
  outfile << endl;
}

void eval(string gt_dir, string result_dir, int c, bool depth, ofstream& outfile) {

  vector<vector<tGroundtruth>> groundtruths;
  vector<vector<tDetection>> detections;
  map<string, vector<vector<tGroundtruth>>> groundtruths_perseq;
  map<string, vector<vector<tDetection>>> detections_perseq;

  cout << "Loading data" << endl;
  string path;
  vector<string> sequences = list_dir(gt_dir);
  size_t idx = 0;
  for (const auto& sequence : sequences) {
    vector<string> frames = list_dir(gt_dir + '/' + sequence);
   
    vector<vector<tGroundtruth>> groundtruths_seq;
    vector<vector<tDetection>> detections_seq;
    for (const auto& frame : frames) {
      string gt_path = gt_dir + '/' + sequence + '/' + frame;
      string result_path = "";
      if (depth) {
        result_path = result_dir + '/' + sequence + '/' + frame;
      } else {
        result_path = result_dir + '/' + sequence + "/image_stitched/" + frame;
      }
      vector<tGroundtruth> gt = loadGroundtruth(gt_path);
      vector<tDetection> det = loadDetection(result_path);
      groundtruths.push_back(gt);
      detections.push_back(det);

      if (frame=="002308.txt") {
        cout << "sequence " << sequence << " idx " << idx << '\n';
        cout << "num boxes in frame " << frame << " gt size " << gt.size() << " dt size " << det.size() << '\n';
      }
      //Build 1to1 map from groundtruths indices to frames in imagesets
      size_t dotPosition = frame.find('.');
      gtdetidx_to_frame_map[idx] = frame.substr(0, dotPosition);
      ++idx;

      groundtruths_seq.push_back(gt);
      detections_seq.push_back(det);
    }
    groundtruths_perseq[sequence] = groundtruths_seq;
    detections_perseq[sequence] = detections_seq;
  }
  cout << "Num gt files " << groundtruths.size() << endl;
  if (groundtruths.size() != N_TESTIMAGES) {
    throw invalid_argument("Mismatch in number of ground truth files.");
  }

  cout << "Loaded data" << endl;

  CLASSES cls = (CLASSES)c;

  // eval image 2D bounding boxes
  if (!depth) {
    cout << "Starting 2D evaluation (" << CLASS_NAMES[c].c_str() << ") ..." << endl;
    vector<double> precision_2d_hard;
    if (!eval_class(cls, groundtruths, detections, false, imageBoxOverlap, precision_2d_hard, IMAGE, HARD, depth)) {
      cout << CLASS_NAMES[c].c_str() << " evaluation failed." << endl;
    } else {
      write_result(outfile, "overall", precision_2d_hard);
    }
    for (auto const& groundtruths_seq : groundtruths_perseq) {
      cout << "Starting per-sequence 2D evaluation (" << groundtruths_seq.first << ", " << CLASS_NAMES[c].c_str() << ") ..." << endl;
      vector<double> precision_2d_seq;
      if (!eval_class(cls, groundtruths_seq.second, detections_perseq[groundtruths_seq.first], false, imageBoxOverlap, precision_2d_seq, IMAGE, HARD, depth)) {
        cout << CLASS_NAMES[c].c_str() << " evaluation failed." << endl;
      } else {
        write_result(outfile, groundtruths_seq.first, precision_2d_seq);
      }
    }
  } else {
    cout << "Starting 3D evaluation (" << CLASS_NAMES[c].c_str() << ") ..." << endl;
    
    // vector<double> precision_3d_easy;
    // vector<double> recall_3d_easy;
    // if (!eval_class(cls, groundtruths, detections, false, box3DOverlap, precision_3d_easy, recall_3d_easy, BOX3D, EASY, depth)) {
    //     cout << CLASS_NAMES[c].c_str() << " evaluation failed." << endl;
    //   } else {
    //     write_result(outfile, "overall", precision_3d_easy, recall_3d_easy);
    //   }
    //   for (auto const& groundtruths_seq : groundtruths_perseq) {
    //     cout << "Starting per-sequence 3D evaluation (" << groundtruths_seq.first << ", " << CLASS_NAMES[c].c_str() << ") ..." << endl;
    //     vector<double> precision_3d_seq;
    //     vector<double> recall_3d_seq;
    //     if (!eval_class(cls, groundtruths_seq.second, detections_perseq[groundtruths_seq.first], false, box3DOverlap, precision_3d_seq, recall_3d_seq, BOX3D, EASY, depth)) {
    //       cout << CLASS_NAMES[c].c_str() << " evaluation failed." << endl;
    //     } else {
    //       write_result(outfile, groundtruths_seq.first, precision_3d_seq, recall_3d_seq);
    //     }
    //   }
    // }
    
    vector<double> precision_3d_hard;
    vector<double> recall_3d_hard;
    if (!eval_class(cls, groundtruths, detections, false, box3DOverlap, precision_3d_hard, recall_3d_hard, BOX3D, HARD, depth, true)) {
      cout << CLASS_NAMES[c].c_str() << " evaluation failed." << endl;
    } else {
      write_result(outfile, "overall", precision_3d_hard, recall_3d_hard);
    }
    for (auto const& groundtruths_seq : groundtruths_perseq) {
      cout << "Starting per-sequence 3D evaluation (" << groundtruths_seq.first << ", " << CLASS_NAMES[c].c_str() << ") ..." << endl;
      vector<double> precision_3d_seq;
      vector<double> recall_3d_seq;
      if (!eval_class(cls, groundtruths_seq.second, detections_perseq[groundtruths_seq.first], false, box3DOverlap, precision_3d_seq, recall_3d_seq, BOX3D, HARD, depth)) {
        cout << CLASS_NAMES[c].c_str() << " evaluation failed." << endl;
      } else {
        write_result(outfile, groundtruths_seq.first, precision_3d_seq, recall_3d_seq);
      }
    }
  }
  
}

// 2D USAGE: ./evaluate_object /path/to/groundtruth /path/to/prediction 0 outfile.txt 0 # iou threshold 0.3
// 2D USAGE: ./evaluate_object /path/to/groundtruth /path/to/prediction 0 outfile.txt 1 # iou threshold 0.5
// 2D USAGE: ./evaluate_object /path/to/groundtruth /path/to/prediction 0 outfile.txt 2 # iou threshold 0.7

// 3D USAGE: ./evaluate_object /path/to/groundtruth /path/to/prediction 1 outfile.txt 0 # iou threshold 0.3
// 3D USAGE: ./evaluate_object /path/to/groundtruth /path/to/prediction 1 outfile.txt 1 # iou threshold 0.5
// 3D USAGE: ./evaluate_object /path/to/groundtruth /path/to/prediction 1 outfile.txt 2 # iou threshold 0.7

int32_t main (int32_t argc, char *argv[]) {
  if (argc != 6) {
    cout << "Usage: ./eval_detection gt_dir result_dir eval_type save_path threshold" << endl;
    return 1;
  }
  initGlobals();

  vector<vector<tGroundtruth>> groundtruths;
  vector<vector<tDetection>> detections;

  bool depth = strcmp(argv[3], "0") != 0;

  // run evaluation
  ofstream outfile;
  outfile.open(argv[4]);
  int i= atoi(argv[5]);
  eval(argv[1], argv[2], i, depth, outfile);
  cout << "Finished evaluating" << endl;
  outfile.close();
  cout << "Saved metrics to " << argv[4] << endl;
  return 0;
}
