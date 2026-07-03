#include "ParameterWindow.h"
#include "ParameterPanel.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>

ParameterWindow::ParameterWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("模型参数"));
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    resize(360, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    // 标题
    auto *titleLabel = new QLabel(QStringLiteral("模型参数"));
    titleLabel->setStyleSheet(
        QStringLiteral("font-weight: bold; font-size: 13px; padding: 4px 0 8px 0;"));
    layout->addWidget(titleLabel);

    // 参数面板（嵌入）
    m_paramPanel = new ParameterPanel(this);
    layout->addWidget(m_paramPanel, 1);
}

void ParameterWindow::setLive2DView(Live2DView *view)
{
    if (m_paramPanel) {
        m_paramPanel->setLive2DView(view);
    }
}

void ParameterWindow::refreshParameterValues()
{
    if (m_paramPanel) {
        m_paramPanel->refreshParameterValues();
    }
}

void ParameterWindow::showAndRaise()
{
    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }
    raise();
    activateWindow();
    // 延迟激活：绕过 Windows 在托盘菜单事件后的焦点防窃取
    QTimer::singleShot(0, this, [this]() {
        activateWindow();
        raise();
    });
}
