#include "ParameterPanel.h"

#include "../pet/Live2DView.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QDebug>
#include <QTimer>
#include <QHash>
#include <QSet>

// ============================================================================
// 参数 ID → 中文名称映射
// ============================================================================
static QString translateParamId(const QString &eng)
{
    static const QHash<QString, QString> map = {
        // --- 头部 ---
        { QStringLiteral("ParamAngleX"),    QStringLiteral("头部左右") },
        { QStringLiteral("ParamAngleY"),    QStringLiteral("头部上下") },
        { QStringLiteral("ParamAngleZ"),    QStringLiteral("头部倾斜") },
        // --- 身体 ---
        { QStringLiteral("ParamBodyAngleX"), QStringLiteral("身体左右") },
        { QStringLiteral("ParamBodyAngleY"), QStringLiteral("身体上下") },
        { QStringLiteral("ParamBodyAngleZ"), QStringLiteral("身体倾斜") },
        // --- 眼睛 ---
        { QStringLiteral("ParamEyeLOpen"),   QStringLiteral("左眼开合") },
        { QStringLiteral("ParamEyeROpen"),   QStringLiteral("右眼开合") },
        { QStringLiteral("ParamEyeBallX"),   QStringLiteral("眼珠左右") },
        { QStringLiteral("ParamEyeBallY"),   QStringLiteral("眼珠上下") },
        { QStringLiteral("ParamEyeForm"),    QStringLiteral("眼睛形状") },
        { QStringLiteral("ParamEyeLForm"),   QStringLiteral("左眼形状") },
        { QStringLiteral("ParamEyeRForm"),   QStringLiteral("右眼形状") },
        { QStringLiteral("ParamEyeLSmile"),  QStringLiteral("左眼笑") },
        { QStringLiteral("ParamEyeRSmile"),  QStringLiteral("右眼笑") },
        // --- 眉毛 ---
        { QStringLiteral("ParamBrowLY"),     QStringLiteral("左眉上下") },
        { QStringLiteral("ParamBrowRY"),     QStringLiteral("右眉上下") },
        { QStringLiteral("ParamBrowLX"),     QStringLiteral("左眉左右") },
        { QStringLiteral("ParamBrowRX"),     QStringLiteral("右眉左右") },
        { QStringLiteral("ParamBrowLAngle"), QStringLiteral("左眉角度") },
        { QStringLiteral("ParamBrowRAngle"), QStringLiteral("右眉角度") },
        { QStringLiteral("ParamBrowLForm"),  QStringLiteral("左眉形状") },
        { QStringLiteral("ParamBrowRForm"),  QStringLiteral("右眉形状") },
        // --- 嘴巴 ---
        { QStringLiteral("ParamMouthOpenY"), QStringLiteral("嘴巴开合") },
        { QStringLiteral("ParamMouthForm"),  QStringLiteral("嘴巴形状") },
        { QStringLiteral("ParamMouthSmile"), QStringLiteral("嘴巴笑") },
        // --- 脸颊 ---
        { QStringLiteral("ParamCheek"),      QStringLiteral("脸颊") },
        // --- 头发 ---
        { QStringLiteral("ParamHairFront"),  QStringLiteral("前发") },
        { QStringLiteral("ParamHairSide"),   QStringLiteral("侧发") },
        { QStringLiteral("ParamHairBack"),   QStringLiteral("后发") },
        { QStringLiteral("ParamHairFluffy"), QStringLiteral("头发蓬松") },
        // --- 手臂 ---
        { QStringLiteral("ParamArmLArmTwist"),  QStringLiteral("左大臂") },
        { QStringLiteral("ParamArmRArmTwist"),  QStringLiteral("右大臂") },
        { QStringLiteral("ParamArmLHandTwist"), QStringLiteral("左手腕") },
        { QStringLiteral("ParamArmRHandTwist"), QStringLiteral("右手腕") },
        // --- 呼吸/胸部/位移 ---
        { QStringLiteral("ParamBreath"),     QStringLiteral("呼吸") },
        { QStringLiteral("ParamBust"),       QStringLiteral("胸部大小") },
        { QStringLiteral("ParamBustY"),      QStringLiteral("胸部上下") },
        { QStringLiteral("ParamBaseX"),      QStringLiteral("位移X") },
        { QStringLiteral("ParamBaseY"),      QStringLiteral("位移Y") },
        // --- Hiyori 特有 ---
        { QStringLiteral("ParamHair"),       QStringLiteral("头发") },
        { QStringLiteral("ParamHairSway"),   QStringLiteral("头发摆动") },
        // --- 其他 ---
        { QStringLiteral("ParamNONE"),       QStringLiteral("无") },
    };

    const auto it = map.constFind(eng);
    if (it != map.constEnd()) {
        return it.value();
    }
    // 未识别的 ID 保持原样
    return eng;
}

// ============================================================================
// 重要参数白名单 — 只保留用户真正需要的属性
// ============================================================================
static bool isImportantParam(const QString &id)
{
    static const QSet<QString> important = {
        // 头部
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamAngleY"),
        QStringLiteral("ParamAngleZ"),
        // 身体
        QStringLiteral("ParamBodyAngleX"),
        QStringLiteral("ParamBodyAngleY"),
        QStringLiteral("ParamBodyAngleZ"),
        // 眼睛（只保留开合，眼珠已移除）
        QStringLiteral("ParamEyeLOpen"),
        QStringLiteral("ParamEyeROpen"),
        // 眉毛（合并为左右一组，单个不显示）
        // 嘴巴
        QStringLiteral("ParamMouthOpenY"),
        QStringLiteral("ParamMouthForm"),
        // 其他常用
        QStringLiteral("ParamBreath"),
        QStringLiteral("ParamBust"),
        QStringLiteral("ParamBustY"),
        QStringLiteral("ParamCheek"),
        // 头发
        QStringLiteral("ParamHairFront"),
        QStringLiteral("ParamHairSide"),
        // 手臂
        QStringLiteral("ParamArmLArmTwist"),
        QStringLiteral("ParamArmRArmTwist"),
    };
    return important.contains(id);
}

// ============================================================================
// 构造
// ============================================================================

ParameterPanel::ParameterPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

// ============================================================================
// UI 搭建
// ============================================================================

void ParameterPanel::buildUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    // ── 可滚动参数列表 ──
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    // 内容容器
    auto *scrollContent = new QWidget();
    auto *outerLayout = new QVBoxLayout(scrollContent);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // 参数行填满容器宽度，滑块撑开名称与数值之间的空隙
    auto *rowsContainer = new QWidget();
    m_rowsLayout = new QVBoxLayout(rowsContainer);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(2);

    outerLayout->addWidget(rowsContainer);
    outerLayout->addStretch(1);

    m_scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(m_scrollArea, 1);

    // ── 重置全部按钮 ──
    m_resetAllBtn = new QPushButton(QStringLiteral("重置全部"));
    m_resetAllBtn->setToolTip(QStringLiteral("清除所有参数的用户覆写"));
    connect(m_resetAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_live2DView) return;
        m_live2DView->clearAllParameterOverrides();
        // 强制立即刷新模型参数（在 GL 上下文中直接调用 ModelParamUpdate）
        m_live2DView->forceModelParamUpdate();
        refreshParameterValues();
    });
    mainLayout->addWidget(m_resetAllBtn);
}

// ============================================================================
// 绑定 / 重建参数列表
// ============================================================================

void ParameterPanel::setLive2DView(Live2DView *view)
{
    m_live2DView = view;

    if (view) {
        connect(view, &Live2DView::modelLoaded, this, [this](double, double, double, double) {
            QTimer::singleShot(0, this, [this]() {
                rebuildParameterList();
            });
        });
    }

    rebuildParameterList();
}

void ParameterPanel::rebuildParameterList()
{
    // ── 清理旧行 ──
    for (auto &row : m_rows) {
        delete row.nameLabel;
        delete row.slider;
        delete row.valueLabel;
    }
    m_rows.clear();

    // 移除旧的占位控件（如有）
    while (m_rowsLayout->count() > 0) {
        QLayoutItem *item = m_rowsLayout->takeAt(0);
        delete item;
    }

    if (!m_live2DView) {
        m_resetAllBtn->setEnabled(false);
        return;
    }

    const int count = m_live2DView->parameterCount();
    bool hasParams = false;

    // ── 第一遍扫描：建立 ID → 模型索引的映射 ──
    QHash<QString, int> idToIndex;
    for (int i = 0; i < count; ++i) {
        idToIndex[m_live2DView->parameterId(i)] = i;
    }

    // ── 定义合并组 ──
    struct MergeGroup {
        QString     displayName;
        QStringList memberIds;   // 原始的左右参数 ID
    };
    const MergeGroup merges[] = {
        { QStringLiteral("眉毛角度"), { QStringLiteral("ParamBrowLAngle"), QStringLiteral("ParamBrowRAngle") } },
        { QStringLiteral("眉毛位置"), { QStringLiteral("ParamBrowLY"),     QStringLiteral("ParamBrowRY") } },
    };

    // ── 找出可用的合并组，收集要跳过的 ID ──
    struct ActiveMerge {
        QString     displayName;
        QVector<int> memberIndices;
    };
    QVector<ActiveMerge> activeMerges;
    QSet<QString> skipIds; // 被合并组覆盖，不单独显示

    for (const auto &mg : merges) {
        QVector<int> indices;
        bool allFound = true;
        for (const auto &id : mg.memberIds) {
            auto it = idToIndex.constFind(id);
            if (it == idToIndex.constEnd()) { allFound = false; break; }
            indices.append(it.value());
            skipIds.insert(id);
        }
        if (allFound) {
            activeMerges.append({ mg.displayName, indices });
        }
    }

    // ── 第二遍：添加独立参数行 ──
    for (int i = 0; i < count; ++i) {
        const float minVal = m_live2DView->parameterMinValue(i);
        const float maxVal = m_live2DView->parameterMaxValue(i);
        if (maxVal - minVal < 0.0001f) continue;

        const QString rawId = m_live2DView->parameterId(i);
        if (!isImportantParam(rawId)) continue;
        if (skipIds.contains(rawId)) continue; // 由合并组接管

        hasParams = true;
        addParamRow({i}, translateParamId(rawId));
    }

    // ── 第三遍：添加合并组行 ──
    for (const auto &am : activeMerges) {
        hasParams = true;
        addParamRow(am.memberIndices, am.displayName);
    }

    m_resetAllBtn->setEnabled(hasParams);

    // 刷新当前值
    refreshParameterValues();
}

// ── 添加一行参数 ──
void ParameterPanel::addParamRow(const QVector<int> &indices, const QString &displayName)
{
    auto *row = new QHBoxLayout();
    row->setSpacing(6);

    auto *nameLabel = new QLabel(displayName);
    nameLabel->setFixedWidth(130);
    nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFontMetrics fm(nameLabel->font());
    nameLabel->setText(fm.elidedText(displayName, Qt::ElideRight, 130));
    nameLabel->setStyleSheet(QStringLiteral("color: #ddd; font-size: 11px;"));

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, kSliderRange);

    auto *valueLabel = new QLabel(QStringLiteral("--"));
    valueLabel->setFixedWidth(56);
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #aaa;"));

    row->addWidget(nameLabel);
    row->addWidget(slider, 1);
    row->addWidget(valueLabel);

    m_rowsLayout->addLayout(row);
    m_rows.append({indices, nameLabel, slider, valueLabel});

    // 滑块拖动 → 同时覆写所有关联的参数
    const QVector<int> capturedIndices = indices;
    connect(slider, &QSlider::valueChanged, this, [this, capturedIndices, valueLabel](int pos) {
        if (!m_live2DView || capturedIndices.isEmpty()) return;
        // 以第一个参数的 min/max 为基准（合并组的左右参数范围通常一致）
        const int firstIdx = capturedIndices.first();
        const float minVal = m_live2DView->parameterMinValue(firstIdx);
        const float maxVal = m_live2DView->parameterMaxValue(firstIdx);
        const float value = sliderToValue(pos, minVal, maxVal);
        valueLabel->setText(QString::number(value, 'f', 3));
        for (int idx : capturedIndices) {
            m_live2DView->setParameterOverride(idx, value);
        }
    });
}

// ============================================================================
// 刷新 / 滑块转换
// ============================================================================

void ParameterPanel::refreshParameterValues()
{
    if (!m_live2DView) return;

    for (const auto &row : m_rows) {
        if (row.paramIndices.isEmpty()) continue;

        // 合并参数取所有成员当前值的平均值
        const int firstIdx = row.paramIndices.first();
        float curValSum = 0;
        for (int idx : row.paramIndices) {
            curValSum += m_live2DView->parameterCurrentValue(idx);
        }
        const float curVal = curValSum / row.paramIndices.size();
        const float minVal = m_live2DView->parameterMinValue(firstIdx);
        const float maxVal = m_live2DView->parameterMaxValue(firstIdx);

        row.valueLabel->setText(QString::number(curVal, 'f', 3));

        row.slider->blockSignals(true);
        row.slider->setValue(valueToSlider(curVal, minVal, maxVal));
        row.slider->blockSignals(false);
    }
}

float ParameterPanel::sliderToValue(int sliderPos, float min, float max) const
{
    const float ratio = static_cast<float>(sliderPos) / static_cast<float>(kSliderRange);
    return min + ratio * (max - min);
}

int ParameterPanel::valueToSlider(float value, float min, float max) const
{
    const float ratio = (value - min) / (max - min);
    return qBound(0, static_cast<int>(ratio * kSliderRange + 0.5f), kSliderRange);
}
