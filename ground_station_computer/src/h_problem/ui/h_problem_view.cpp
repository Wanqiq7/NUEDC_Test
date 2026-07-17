#include "h_problem/ui/h_problem_view.h"

#include "h_problem/ui/h_grid_scene.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QComboBox>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

constexpr int kMaxDetectionListItems = 500;

// 自适应视口的 QGraphicsView：窗口尺寸变化时保持场景等比铺满，避免出现滚动条。
class AutoFitGraphicsView final : public QGraphicsView {
public:
    explicit AutoFitGraphicsView(QGraphicsScene *scene, QWidget *parent = nullptr)
        : QGraphicsView(scene, parent) {
        setAlignment(Qt::AlignCenter);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QGraphicsView::resizeEvent(event);
        fitSceneToViewport();
    }

    void showEvent(QShowEvent *event) override {
        QGraphicsView::showEvent(event);
        fitSceneToViewport();
    }

private:
    void fitSceneToViewport() {
        if (scene() == nullptr || scene()->sceneRect().isEmpty()) {
            return;
        }
        resetTransform();
        fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    }
};

} // namespace

QWidget *HProblemView::buildWidget(QWidget *parent, const QMap<QString, int> &initial_totals) {
    grid_scene_ = new GridScene(parent);
    grid_scene_->setObjectName("GridScene");
    auto *view = new AutoFitGraphicsView(grid_scene_, parent);
    view->setObjectName("TaskView");
    view->setRenderHints(QPainter::Antialiasing);
    view->setFrameShape(QFrame::NoFrame);
    view->setStyleSheet(
        "QGraphicsView {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        "    stop:0 #f8fbff, stop:1 #edf3fb);"
        "  border: 1px solid #d8e1ee;"
        "  border-radius: 18px;"
        "}"
    );
    QObject::connect(grid_scene_, &GridScene::cellClicked, view, [this](const QString &cell_code) {
        if (cell_clicked_handler_) {
            cell_clicked_handler_(cell_code);
        }
    });

    case_label_ = new QLabel("案例: 未加载", parent);
    case_label_->setObjectName("StatusText");
    case_label_->setWordWrap(true);
    mission_label_ = new QLabel("任务: 等待规划", parent);
    mission_label_->setObjectName("StatusText");
    mission_label_->setWordWrap(true);
    target_status_label_ = new QLabel("目标: 等待跟踪", parent);
    target_status_label_->setObjectName("TargetStatusLabel");
    target_status_label_->setWordWrap(true);

    auto *legend_label = new QLabel(parent);
    legend_label->setWordWrap(true);
    legend_label->setText(
        "<div style='line-height:1.7;'>"
        "<b style='color:#0f172a;'>路线图例</b><br/>"
        "<span style='color:#2f6fed;'>■</span> 主航线"
        "　<span style='color:#f08c00;'>■</span> 重复航段<br/>"
        "<span style='color:#14906f;'>■</span> 终点 / 45° 降落走廊"
        "<br/><span style='color:#475569;'>橙色圆牌显示重复次数，如 2x / 3x；箭头表示飞行方向。</span>"
        "</div>");

    detection_list_ = new QListWidget(parent);
    summary_table_ = new QTableWidget(0, 2, parent);
    summary_table_->setObjectName("DetectionSummaryTable");
    summary_table_->setHorizontalHeaderLabels({"动物", "数量"});
    summary_table_->horizontalHeader()->setStretchLastSection(true);
    summary_table_->verticalHeader()->setVisible(false);
    summary_table_->setAlternatingRowColors(true);
    summary_table_->setSelectionMode(QAbstractItemView::NoSelection);
    setSummaryTotals(initial_totals);

    auto *overview_card = new QFrame(parent);
    overview_card->setProperty("card", true);
    auto *overview_layout = new QVBoxLayout(overview_card);
    overview_layout->setContentsMargins(16, 16, 16, 16);
    overview_layout->setSpacing(8);
    auto *overview_title = new QLabel("任务概览", parent);
    overview_title->setObjectName("CardTitle");
    overview_layout->addWidget(overview_title);
    overview_layout->addWidget(case_label_);
    overview_layout->addWidget(mission_label_);
    overview_layout->addWidget(target_status_label_);
    overview_layout->addSpacing(4);
    overview_layout->addWidget(legend_label);

    auto *detection_card = new QFrame(parent);
    detection_card->setProperty("card", true);
    auto *detection_layout = new QVBoxLayout(detection_card);
    detection_layout->setContentsMargins(16, 16, 16, 16);
    detection_layout->setSpacing(10);
    auto *detection_title = new QLabel("实时检测记录", parent);
    detection_title->setObjectName("CardTitle");
    detection_layout->addWidget(detection_title);
    detection_layout->addWidget(detection_list_, 1);

    auto *summary_card = new QFrame(parent);
    summary_card->setProperty("card", true);
    auto *summary_layout = new QVBoxLayout(summary_card);
    summary_layout->setContentsMargins(16, 16, 16, 16);
    summary_layout->setSpacing(10);
    auto *summary_title = new QLabel("统计汇总", parent);
    summary_title->setObjectName("CardTitle");
    summary_layout->addWidget(summary_title);
    summary_layout->addWidget(summary_table_);
    auto *query_button = new QPushButton("动物查询", parent);
    query_button->setObjectName("AnimalQueryButton");
    query_button->setFixedHeight(40);
    query_button->setCursor(Qt::PointingHandCursor);
    summary_layout->addWidget(query_button);

    auto *right_layout = new QVBoxLayout();
    right_layout->setSpacing(12);
    right_layout->addWidget(overview_card);
    right_layout->addWidget(detection_card, 1);
    right_layout->addWidget(summary_card);

    auto *mission_page = new QWidget(parent);
    auto *mission_layout = new QHBoxLayout(mission_page);
    mission_layout->setContentsMargins(0, 0, 0, 0);
    mission_layout->setSpacing(16);
    mission_layout->addWidget(view, 3);
    mission_layout->addLayout(right_layout, 2);

    auto *query_page = new QWidget(parent);
    query_page->setObjectName("AnimalQueryPage");
    auto *query_layout = new QVBoxLayout(query_page);
    query_layout->setContentsMargins(24, 20, 24, 20);
    query_layout->setSpacing(14);
    auto *query_header = new QHBoxLayout();
    auto *back_button = new QPushButton("返回巡查", query_page);
    back_button->setObjectName("AnimalQueryBackButton");
    back_button->setFixedHeight(40);
    auto *query_title = new QLabel("动物位置查询", query_page);
    query_title->setObjectName("PanelTitle");
    query_header->addWidget(back_button);
    query_header->addWidget(query_title);
    query_header->addStretch();
    query_layout->addLayout(query_header);
    animal_species_combo_ = new QComboBox(query_page);
    animal_species_combo_->setObjectName("AnimalSpeciesCombo");
    animal_species_combo_->setMinimumHeight(40);
    query_layout->addWidget(animal_species_combo_);
    animal_query_total_ = new QLabel("总数: 0", query_page);
    animal_query_total_->setObjectName("AnimalQueryTotal");
    query_layout->addWidget(animal_query_total_);
    animal_location_table_ = new QTableWidget(0, 2, query_page);
    animal_location_table_->setObjectName("AnimalLocationTable");
    animal_location_table_->setHorizontalHeaderLabels({"方格坐标", "数量"});
    animal_location_table_->horizontalHeader()->setStretchLastSection(true);
    animal_location_table_->verticalHeader()->setVisible(false);
    animal_location_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    animal_location_table_->setSelectionMode(QAbstractItemView::NoSelection);
    query_layout->addWidget(animal_location_table_, 1);

    pages_ = new QStackedWidget(parent);
    pages_->setObjectName("HProblemPages");
    pages_->addWidget(mission_page);
    pages_->addWidget(query_page);
    QObject::connect(query_button, &QPushButton::clicked, pages_, [this] { openAnimalQuery(); });
    QObject::connect(back_button, &QPushButton::clicked, pages_, [this] {
        pages_->setCurrentIndex(0);
    });
    QObject::connect(
        animal_species_combo_,
        &QComboBox::currentTextChanged,
        pages_,
        [this](const QString &) { refreshAnimalQuery(); });

    auto *root = new QWidget(parent);
    auto *root_layout = new QVBoxLayout(root);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->addWidget(pages_);
    return root;
}

void HProblemView::setAnimalQueryProviders(
    AnimalNamesProvider names_provider,
    AnimalLocationsProvider locations_provider) {
    animal_names_provider_ = std::move(names_provider);
    animal_locations_provider_ = std::move(locations_provider);
}

void HProblemView::openAnimalQuery() {
    if (pages_ == nullptr || animal_species_combo_ == nullptr) {
        return;
    }
    const QString previous = animal_species_combo_->currentText();
    animal_species_combo_->blockSignals(true);
    animal_species_combo_->clear();
    if (animal_names_provider_) {
        animal_species_combo_->addItems(animal_names_provider_());
    }
    const int previous_index = animal_species_combo_->findText(previous);
    if (previous_index >= 0) {
        animal_species_combo_->setCurrentIndex(previous_index);
    }
    animal_species_combo_->blockSignals(false);
    refreshAnimalQuery();
    pages_->setCurrentIndex(1);
}

void HProblemView::refreshAnimalQuery() {
    if (animal_species_combo_ == nullptr || animal_query_total_ == nullptr ||
        animal_location_table_ == nullptr) {
        return;
    }
    const QMap<QString, int> locations = animal_locations_provider_
        ? animal_locations_provider_(animal_species_combo_->currentText())
        : QMap<QString, int>{};
    int total = 0;
    animal_location_table_->setRowCount(locations.size());
    int row = 0;
    for (auto iterator = locations.cbegin(); iterator != locations.cend(); ++iterator, ++row) {
        total += iterator.value();
        animal_location_table_->setItem(row, 0, new QTableWidgetItem(iterator.key()));
        animal_location_table_->setItem(row, 1, new QTableWidgetItem(QString::number(iterator.value())));
    }
    animal_query_total_->setText(QString("总数: %1").arg(total));
}

void HProblemView::setCaseLabel(const QString &text) {
    if (case_label_ != nullptr) {
        case_label_->setText(text);
    }
}

void HProblemView::setMissionLabel(const QString &text) {
    if (mission_label_ != nullptr) {
        mission_label_->setText(text);
    }
}

void HProblemView::setTargetStatus(const QString &text) {
    if (target_status_label_ != nullptr) {
        target_status_label_->setText(text);
    }
}

void HProblemView::showRoute(
    const QStringList &no_fly_cells,
    const QStringList &route,
    const QString &start_cell,
    const QString &descent_start_cell,
    double touchdown_x_cm,
    double touchdown_y_cm,
    bool landing_enabled) {
    if (grid_scene_ == nullptr) {
        return;
    }
    grid_scene_->clearCandidateNoFlyCells();
    grid_scene_->setNoFlyEditEnabled(false);
    grid_scene_->setNoFlyCells(no_fly_cells);
    grid_scene_->setStartCell(start_cell);
    grid_scene_->setRoute(route);
    grid_scene_->setLandingTarget(
        start_cell,
        descent_start_cell,
        touchdown_x_cm,
        touchdown_y_cm,
        landing_enabled);
    grid_scene_->setCurrentCell(start_cell);
}

void HProblemView::enterNoFlyEditMode() {
    if (grid_scene_ == nullptr) {
        return;
    }
    grid_scene_->clearCandidateNoFlyCells();
    grid_scene_->setNoFlyCells({});
    grid_scene_->setNoFlyEditEnabled(true);
}

void HProblemView::setCandidateCells(const QStringList &cells) {
    if (grid_scene_ != nullptr) {
        grid_scene_->setCandidateNoFlyCells(cells);
    }
}

void HProblemView::setCurrentCell(const QString &cell_code) {
    if (grid_scene_ != nullptr) {
        grid_scene_->setCurrentCell(cell_code);
    }
}

void HProblemView::appendDetection(const QString &text) {
    if (detection_list_ == nullptr) {
        return;
    }
    detection_list_->addItem(text);
    while (detection_list_->count() > kMaxDetectionListItems) {
        delete detection_list_->takeItem(0);
    }
}

void HProblemView::setSummaryTotals(const QMap<QString, int> &totals) {
    if (summary_table_ == nullptr) {
        return;
    }
    summary_table_->setRowCount(totals.size());
    int row = 0;
    for (auto it = totals.begin(); it != totals.end(); ++it, ++row) {
        summary_table_->setItem(row, 0, new QTableWidgetItem(it.key()));
        summary_table_->setItem(row, 1, new QTableWidgetItem(QString::number(it.value())));
    }
}
