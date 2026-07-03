#pragma once

#include <QWidget>

class ParameterPanel;
class Live2DView;

/**
 * @brief ParameterWindow — Live2D 模型参数调节的独立窗口。
 *
 * 包装 ParameterPanel 为一个独立窗口，通过托盘菜单打开，
 * 与 SettingsWindow 分离，方便随时调参。
 */
class ParameterWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ParameterWindow(QWidget *parent = nullptr);

    void setLive2DView(Live2DView *view);
    void refreshParameterValues();

public slots:
    void showAndRaise();

private:
    ParameterPanel *m_paramPanel = nullptr;
};
