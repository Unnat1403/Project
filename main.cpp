// main.cpp
// FitTrack Pro - dashboard stacked cards + centered headings + bodyweight weekly bars only
// Updated: bodyweight card shows a 7-day daily-weight bar chart (no "vs 7d ago" line)
#include <QMetaObject>
#include <QThread>
#include <QtWidgets>
#include <vector>

// --- Lightweight 7-day bar chart widget (no external libs) ---
class WeeklyBarChart : public QWidget {
    Q_OBJECT
public:
    explicit WeeklyBarChart(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(48);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    // data: 7 values, index 0 = 6 days ago, index 6 = today
    void setData(const QVector<double> &vals) {
        if (vals.size() == 7) data = vals;
        else {
            data = QVector<double>(7, 0.0);
            for (int i = 0; i < vals.size() && i < 7; ++i) data[7 - vals.size() + i] = vals[i];
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect().marginsRemoved(QMargins(6,6,6,6));

        // background subtle
        p.fillRect(r, QColor(7, 27, 34, 16));

        const int bars = 7;
        if (data.size() != bars) {
            // draw empty placeholder bars
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(40, 60, 80));
            for (int i = 0; i < bars; ++i) {
                QRect b(r.left() + i*(r.width()/bars), r.top(), r.width()/bars - 6, r.height());
                p.drawRect(b);
            }
            return;
        }

        double maxv = 0.0;
        for (double v : data) maxv = qMax(maxv, v);
        if (maxv <= 0.0) maxv = 1.0; // avoid div by zero

        int gap = 6;
        int bw = qMax(4, (r.width() - (bars-1)*gap) / bars);
        for (int i = 0; i < bars; ++i) {
            double val = data[i];
            int barH = int((val / maxv) * (r.height() - 6));
            QRect barRect(r.left() + i*(bw+gap), r.bottom() - barH, bw, barH);

            // highlight today (index 6) and use a warm color for today
            QColor fill = (i == bars-1) ? QColor(255,95,31) : QColor(12,80,100);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(barRect, 3, 3);

            // small top accent
            p.setBrush(fill.lighter(140));
            QRect cap(barRect.left(), barRect.top(), barRect.width(), qMax(2, barRect.height()/8));
            p.drawRect(cap);
        }

        // thin baseline
        p.setPen(QPen(QColor(20,50,70), 1));
        p.drawLine(r.left(), r.bottom()+1, r.right(), r.bottom()+1);
    }

private:
    QVector<double> data; // length 7
};

// --- Data structures ---
struct ExerciseSet { int reps; double weight; };
struct Exercise { QString name; std::vector<ExerciseSet> sets; };
struct StrengthWorkout { QString date; std::vector<Exercise> exercises; double calories; };
struct CardioWorkout { QString date; QString type; int duration; double distance; double calories; double avgSpeed; };
struct BodyweightLog { QString date; double weight; };

struct Goal {
    QString name;
    QString type; // "cardio_km", "strength_exercise"
    double target = 0;
    double progress = 0;
    int targetTime = 0;
    int progressTime = 0;
    QString exerciseName;
    double exWeight = 0;
    int exSets = 0;
    int exReps = 0;
};

// BackgroundWidget: paints a scaled pixmap that covers the widget (like CSS background-size: cover)
class BackgroundWidget : public QWidget {
    Q_OBJECT
public:
    explicit BackgroundWidget(const QString &resourcePath, QWidget *parent = nullptr)
        : QWidget(parent), base(resourcePath) {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setContentsMargins(0,0,0,0);
    }

protected:
    void paintEvent(QPaintEvent *ev) override {
        Q_UNUSED(ev);
        QPainter p(this);
        if (base.isNull()) { p.fillRect(rect(), palette().window()); return; }
        if (scaled.isNull()) updateScaledPixmap();
        if (!scaled.isNull()) {
            int x = (width() - scaled.width()) / 2;
            int y = (height() - scaled.height()) / 2;
            p.drawPixmap(x, y, scaled);
        } else {
            p.fillRect(rect(), palette().window());
        }
    }

    void resizeEvent(QResizeEvent *ev) override {
        QWidget::resizeEvent(ev);
        updateScaledPixmap();
        update();
    }

private:
    void updateScaledPixmap() {
        if (base.isNull() || width() <= 0 || height() <= 0) { scaled = QPixmap(); return; }
        // KeepAspectRatioByExpanding -> scaled to cover (may crop edges)
        scaled = base.scaled(width(), height(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }

    QPixmap base;
    QPixmap scaled;
};

struct UserProfile { QString username; QString name; QString gender; double weight = 0; double targetBodyweight = 0; double height = 0; int age = 0; };

// --- Main Window ---
class FitTrackPro : public QMainWindow {
    Q_OBJECT
public:
    FitTrackPro() {
        setWindowTitle("FitTrack Pro");
        setMinimumSize(980, 680);

        // Inline SVG icons for plus/minus arrows (white).
        const QString upSvg = "data:image/svg+xml;utf8,"
                              "<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14'>"
                              "<text x='50%' y='50%' font-size='12' text-anchor='middle' alignment-baseline='central' fill='%23FFFFFF'>+</text>"
                              "</svg>";
        const QString downSvg = "data:image/svg+xml;utf8,"
                                "<svg xmlns='http://www.w3.org/2000/svg' width='14' height='14'>"
                                "<text x='50%' y='50%' font-size='12' text-anchor='middle' alignment-baseline='central' fill='%23FFFFFF'>-</text>"
                                "</svg>";

        // Build stylesheet
        QString style;
        style.append(
            "QMainWindow, QWidget { background: #071126; color: #ffffff; font-family: 'Segoe UI', Roboto, Arial; }"
            "QGroupBox { background: #0b1b2b; border: 1px solid #163247; border-radius: 10px; margin-top: 12px; padding-top: 10px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0px 6px; color: #ffffff; font-weight: 900; font-size: 13px; }"
            "QLabel, QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QTableWidget, QHeaderView::section, QListWidget { color: #ffffff; }"
            "QTabWidget::pane { border: none; background: transparent; }"
            "QTabBar::tab { background: #071126; color: #ffffff; padding: 12px 22px; margin: 4px; border-radius: 8px; font-weight: 900; font-size: 14.5px; }"
            "QTabBar::tab:selected { background: #0f2a40; }"
            "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QDateEdit { background: #071b2a; border: 1px solid #163247; border-radius: 8px; padding: 8px; color: #ffffff; }"
            "QLineEdit::placeholder { color: #b8c8d8; }"
            "QTableWidget { background: #05121b; alternate-background-color: #081826; gridline-color: #10314a; border-radius: 8px; selection-background-color: #FF833F; }"
            "QHeaderView::section { background: #0c3550; color: #ffffff; font-weight: 900; padding: 10px; border: none; font-size: 13px; }"
            "QListWidget { background: #07121b; border: 1px solid #163247; border-radius: 8px; }"
            "QProgressBar { background: #061223; border: 1px solid #163247; border-radius: 8px; text-align: center; min-height: 22px; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #FF5F1F, stop:1 #FF8A3D); }"
            "QPushButton { background: #FF5F1F; color: #ffffff; border: none; border-radius: 999px; padding: 10px 18px; font-weight: 900; font-size: 14px; }"
            "QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
            "  background: #0f2a40; min-width: 36px; min-height: 36px; border-left: 1px solid #163247; border-radius: 6px; }"
            );

        // arrow images
        style.append(QString("QSpinBox::up-arrow { image: url(\"%1\"); width: 14px; height: 14px; }").arg(upSvg));
        style.append(QString("QSpinBox::down-arrow { image: url(\"%1\"); width: 14px; height: 14px; }").arg(downSvg));
        style.append(QString("QDoubleSpinBox::up-arrow { image: url(\"%1\"); width: 14px; height: 14px; }").arg(upSvg));
        style.append(QString("QDoubleSpinBox::down-arrow { image: url(\"%1\"); width: 14px; height: 14px; }").arg(downSvg));

        setStyleSheet(style);

        stack = new QStackedWidget(this);
        setCentralWidget(stack);

        buildLoginPage();
        buildSignupPage();
        buildProfilePage();
        buildMainPage();

        stack->addWidget(loginPage);
        stack->addWidget(signupPage);
        stack->addWidget(profilePage);
        stack->addWidget(mainPage);

        applyCompactRoundedButtons();

        QFont tableFont = QApplication::font();
        tableFont.setPointSize(11);
        for (QTableWidget *t : this->findChildren<QTableWidget*>()) {
            t->setFont(tableFont);
            t->verticalHeader()->setDefaultSectionSize(36);
            t->setAlternatingRowColors(true);
        }
    }

private:
    // Data
    UserProfile user;
    std::vector<CardioWorkout> cardio;
    std::vector<StrengthWorkout> strength;
    std::vector<BodyweightLog> weightLogs;
    std::vector<Goal> goals;
    std::vector<Exercise> curEx;
    QString pUser, pName;

    // Widgets
    QStackedWidget *stack = nullptr;
    QWidget *loginPage = nullptr, *signupPage = nullptr, *profilePage = nullptr, *mainPage = nullptr;
    QLineEdit *logUser = nullptr, *logPass = nullptr, *sigName = nullptr, *sigUser = nullptr, *sigPass = nullptr, *sigConf = nullptr;
    QComboBox *profGender = nullptr, *cardioTypeCb = nullptr, *goalTypeCb = nullptr, *editGender = nullptr;
    QDoubleSpinBox *profWeight = nullptr, *profHeight = nullptr, *cardioDist = nullptr, *exWeight = nullptr, *editWeight = nullptr, *editHeight = nullptr;
    QSpinBox *profAge = nullptr, *cardioDur = nullptr, *exSets = nullptr, *editAge = nullptr;
    QDateEdit *cardioDateEd = nullptr, *strDateEd = nullptr;
    QLineEdit *exName = nullptr, *goalNameEd = nullptr;
    QTableWidget *setsT = nullptr, *cardioT = nullptr, *strT = nullptr, *goalsT = nullptr, *weightT = nullptr;
    QListWidget *exList = nullptr;
    QLabel *welLblMain = nullptr, *userLbl = nullptr, *cCnt = nullptr, *cDist = nullptr, *cCal = nullptr, *sCnt = nullptr, *sVol = nullptr, *sCal = nullptr, *bmiLbl = nullptr, *bmiCat = nullptr;
    QProgressBar *cGoalBar = nullptr, *sGoalBar = nullptr;

    // Dashboard weekly widgets (new members)
    QLabel *cardioWeeklyLbl = nullptr;
    QLabel *cardioWeeklySub = nullptr;
    QLabel *strengthWeeklyLbl = nullptr;
    QLabel *strengthWeeklySub = nullptr;
    QLabel *bwCurrentLbl = nullptr;
    WeeklyBarChart *cardioChart = nullptr;
    WeeklyBarChart *strengthChart = nullptr;
    WeeklyBarChart *bwChart = nullptr;

    QStackedWidget *goalSwitchStack = nullptr;
    QWidget *goalCardioPage = nullptr;
    QWidget *goalStrengthPage = nullptr;

    QDoubleSpinBox *goalTargetSp = nullptr;
    QSpinBox *goalTargetTimeSp = nullptr;
    QLineEdit *goalExNameEd = nullptr;
    QDoubleSpinBox *goalExWeightSp = nullptr;
    QSpinBox *goalExSetsSp = nullptr;
    QSpinBox *goalExRepsSp = nullptr;

    QCheckBox *perSetWeightCb = nullptr;

    // Profile/bodyweight controls
    QDoubleSpinBox *targetBodyweightSp = nullptr;
    QDateEdit *bwDateEd = nullptr;
    QDoubleSpinBox *bwWeightSp = nullptr;
    QDoubleSpinBox *editTargetBodyweight = nullptr;

    // Utility: wrap spinboxes so they adopt consistent look and min width
    QWidget* wrapSpinBox(QSpinBox *sp) {
        sp->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        sp->setMinimumWidth(110);
        sp->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        return sp;
    }
    QWidget* wrapDoubleSpinBox(QDoubleSpinBox *sp) {
        sp->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        sp->setMinimumWidth(110);
        sp->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        return sp;
    }

    void applyCompactRoundedButtons() {
        const QString baseStyle =
            "background: #FF5F1F;"
            "color: #ffffff;"
            "border: none;"
            "border-radius: 999px;"
            "padding: 8px 16px;"
            "font-weight: 900;"
            "font-size: 14px;";

        auto btns = this->findChildren<QPushButton*>();
        for (QPushButton *b : btns) {
            if (!b) continue;
            b->setStyleSheet(baseStyle);
            b->setCursor(Qt::PointingHandCursor);
            QWidget *parent = b->parentWidget();
            if (parent) {
                QWidget *w = parent;
                while (w && !w->layout()) w = w->parentWidget();
                if (w && w->layout()) {
                    // try to center the button only where it already sits
                    w->layout()->setAlignment(b, Qt::AlignHCenter | Qt::AlignVCenter);
                }
            }
        }
    }

    // Small struct to return card and child widgets (avoids casts)
    struct CardWidgets {
        QGroupBox* card = nullptr;
        QLabel* bigLbl = nullptr;
        QProgressBar* bar = nullptr; // may be null
        QLabel* subLbl = nullptr;
    };

    // Build UI pages
    QWidget* buildLoginPage() {
        loginPage = new BackgroundWidget(":/gymbg.jpeg");
        loginPage->setObjectName("loginPageBG");
        loginPage->setStyleSheet(
            "QWidget#loginPageBG { "
            "background-image: url(:/gymbg.jpeg);"
            "background-repeat: no-repeat;"
            "background-position: center;"
            "background-size: cover;"
            "}"
            );

        auto *lo = new QVBoxLayout(loginPage);
        lo->setAlignment(Qt::AlignCenter);

        auto *box = new QWidget;
        box->setMaximumWidth(480);
        box->setStyleSheet("background: rgba(7,18,30,0.86); border-radius:12px; padding:22px;");

        auto *fl = new QVBoxLayout(box);
        fl->setSpacing(10);

        auto *tit = new QLabel("Login");
        tit->setStyleSheet("font-size:32px; font-weight:900; color:#ffffff;");
        tit->setAlignment(Qt::AlignCenter);
        fl->addWidget(tit);
        fl->addSpacing(6);

        auto *grp = new QGroupBox;
        auto *gl = new QVBoxLayout(grp);
        gl->setSpacing(8);

        QLabel *uLbl = new QLabel("Username:");
        uLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        gl->addWidget(uLbl);
        logUser = new QLineEdit;
        logUser->setPlaceholderText("Enter Username");
        logUser->setStyleSheet("font-weight:700; color:#ffffff;");
        gl->addWidget(logUser);

        QLabel *pLbl = new QLabel("Password:");
        pLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        gl->addWidget(pLbl);
        logPass = new QLineEdit;
        logPass->setEchoMode(QLineEdit::Password);
        logPass->setPlaceholderText("Enter Password");
        logPass->setStyleSheet("font-weight:700; color:#ffffff;");
        gl->addWidget(logPass);

        fl->addWidget(grp);

        // Login button
        auto *btnLogin = new QPushButton("Login");
        btnLogin->setObjectName("loginBtn");
        connect(btnLogin, &QPushButton::clicked, [this]{ doLogin(); });
        fl->addWidget(btnLogin);

        auto *noAccount = new QLabel("Don't Have An Account? Sign Up");
        noAccount->setAlignment(Qt::AlignCenter);
        noAccount->setStyleSheet("color: #c9dbe9; font-size:12px;");
        fl->addWidget(noAccount);

        // Sign up button
        auto *btnSignup = new QPushButton("Sign Up");
        btnSignup->setObjectName("signupBtn");
        connect(btnSignup, &QPushButton::clicked, [this]{ stack->setCurrentWidget(signupPage); });
        fl->addWidget(btnSignup);

        lo->addWidget(box);
        return loginPage;
    }

    void buildSignupPage() {
        signupPage = new BackgroundWidget(":/gymbg.jpeg");
        signupPage->setObjectName("signupPageBG");
        signupPage->setStyleSheet(
            "QWidget#signupPageBG { "
            "background-image: url(:/gymbg.jpeg);"
            "background-repeat: no-repeat;"
            "background-position: center;"
            "background-size: cover;"
            "}"
            );


        auto *lo = new QVBoxLayout(signupPage);
        lo->setAlignment(Qt::AlignCenter);
        auto *box = new QWidget; box->setMaximumWidth(620);
        box->setStyleSheet("background: rgba(7,18,30,0.86); border-radius:12px; padding:18px;");
        auto *fl = new QVBoxLayout(box);

        auto *tit = new QLabel("Sign Up");
        tit->setStyleSheet("font-size:30px; font-weight:900; color:#ffffff;");
        tit->setAlignment(Qt::AlignCenter);
        fl->addWidget(tit);
        fl->addSpacing(8);

        QGroupBox *grp = new QGroupBox;
        auto *g = new QGridLayout(grp);
        g->setHorizontalSpacing(12);
        g->setVerticalSpacing(8);

        QLabel *nameLbl = new QLabel("Name:");
        nameLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        sigName = new QLineEdit; sigName->setStyleSheet("font-weight:700; color:#ffffff;");
        g->addWidget(nameLbl, 0, 0); g->addWidget(sigName, 0, 1);

        QLabel *usernameLbl = new QLabel("Username:");
        usernameLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        sigUser = new QLineEdit; sigUser->setStyleSheet("font-weight:700; color:#ffffff;");
        g->addWidget(usernameLbl, 1, 0); g->addWidget(sigUser, 1, 1);

        QLabel *passwordLbl = new QLabel("Password:");
        passwordLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        sigPass = new QLineEdit; sigPass->setEchoMode(QLineEdit::Password); sigPass->setStyleSheet("font-weight:700; color:#ffffff;");
        g->addWidget(passwordLbl, 2, 0); g->addWidget(sigPass, 2, 1);

        QLabel *confirmLbl = new QLabel("Confirm:");
        confirmLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        sigConf = new QLineEdit; sigConf->setEchoMode(QLineEdit::Password); sigConf->setStyleSheet("font-weight:700; color:#ffffff;");
        g->addWidget(confirmLbl, 3, 0); g->addWidget(sigConf, 3, 1);

        fl->addWidget(grp);

        auto *btn1 = new QPushButton("Create");
        connect(btn1, &QPushButton::clicked, [this]{ doSignup(); });
        fl->addWidget(btn1);

        auto *btn2 = new QPushButton("Back");
        connect(btn2, &QPushButton::clicked, [this]{ stack->setCurrentWidget(loginPage); });
        fl->addWidget(btn2);

        lo->addWidget(box);
    }

    void buildProfilePage() {
        profilePage = new BackgroundWidget(":/gymbg.jpeg");
        auto *lo = new QVBoxLayout(profilePage);
        profilePage->setStyleSheet(
            "QWidget#profilePageBG { "
            "background-image: url(:/gymbg.jpeg);"
            "background-repeat: no-repeat;"
            "background-position: center;"
            "background-size: cover;"
            "}"
            );
        profilePage->setObjectName("profilePageBG");
        lo->setAlignment(Qt::AlignCenter);
        auto *box = new QWidget; box->setMaximumWidth(620); // increased width to match signup
        box->setStyleSheet("background: rgba(7,18,30,0.86); border-radius:12px; padding:18px;");
        auto *fl = new QVBoxLayout(box);

        auto *tit = new QLabel("Complete Profile");
        tit->setStyleSheet("font-size:28px;font-weight:900;color:#ffffff");
        tit->setAlignment(Qt::AlignCenter);
        fl->addWidget(tit);

        auto *grpTitle = new QLabel("Your Details");
        grpTitle->setAlignment(Qt::AlignCenter);
        grpTitle->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        fl->addWidget(grpTitle);

        auto *grp = new QGroupBox;
        auto *g = new QGridLayout(grp);
        g->setHorizontalSpacing(12);
        g->setVerticalSpacing(8);

        // Create centered & bold labels for details
        auto addBoldCenteredLabel = [&](const QString &text, int row, int col) {
            QLabel *lbl = new QLabel(text);
            lbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
            lbl->setAlignment(Qt::AlignCenter);
            g->addWidget(lbl, row, col);
        };

        addBoldCenteredLabel("Gender:", 0, 0);
        profGender = new QComboBox; profGender->addItems({"Male","Female","Other"}); g->addWidget(profGender, 0, 1);

        addBoldCenteredLabel("Weight (Kg):", 1, 0);
        profWeight = new QDoubleSpinBox; profWeight->setRange(20,300); profWeight->setValue(70);
        g->addWidget(wrapDoubleSpinBox(profWeight), 1, 1);

        // NEW: Target Bodyweight in Profile (user requested)
        addBoldCenteredLabel("Target Bodyweight (Kg):", 2, 0);
        targetBodyweightSp = new QDoubleSpinBox; targetBodyweightSp->setRange(20,300); targetBodyweightSp->setDecimals(1); targetBodyweightSp->setValue(70);
        g->addWidget(wrapDoubleSpinBox(targetBodyweightSp), 2, 1);

        addBoldCenteredLabel("Height (Cm):", 3, 0);
        profHeight = new QDoubleSpinBox; profHeight->setRange(100,250); profHeight->setValue(170);
        g->addWidget(wrapDoubleSpinBox(profHeight), 3, 1);

        addBoldCenteredLabel("Age:", 4, 0);
        profAge = new QSpinBox; profAge->setRange(10,120); profAge->setValue(25);
        g->addWidget(wrapSpinBox(profAge), 4, 1);

        fl->addWidget(grp);

        auto *btn = new QPushButton("Start Tracking");
        connect(btn, &QPushButton::clicked, [this]{ doCompleteProfile(); });
        fl->addWidget(btn);

        lo->addWidget(box);
    }

    void buildMainPage() {
        mainPage = new QWidget;
        auto *lo = new QVBoxLayout(mainPage);

        // Top centered welcome area (two lines): "Welcome" and username beneath (centered)
        auto *welcomeWidget = new QWidget;
        auto *welcomeLayout = new QVBoxLayout(welcomeWidget);
        welcomeLayout->setAlignment(Qt::AlignCenter);
        welLblMain = new QLabel("Welcome");
        welLblMain->setStyleSheet("font-size:20px;font-weight:900;color:#ffffff");
        welLblMain->setAlignment(Qt::AlignCenter);
        userLbl = new QLabel(""); // filled after login/profile complete
        userLbl->setStyleSheet("font-size:16px;font-weight:700;color:#ffffff");
        userLbl->setAlignment(Qt::AlignCenter);
        welcomeLayout->addWidget(welLblMain);
        welcomeLayout->addWidget(userLbl);
        lo->addWidget(welcomeWidget);

        auto *tabs = new QTabWidget;
        tabs->addTab(buildDashboard(), "Dashboard");
        tabs->addTab(buildLogTab(), "Log");
        tabs->addTab(buildGoalsTab(), "Goals");
        tabs->addTab(buildProfileTab(), "Profile");
        lo->addWidget(tabs);
    }

    QWidget* buildLogTab() {
        auto *w = new QWidget;
        auto *lo = new QVBoxLayout(w);
        auto *subTabs = new QTabWidget;
        subTabs->addTab(buildCardioTab(), "Cardio");
        subTabs->addTab(buildStrengthTab(), "Strength");
        subTabs->addTab(buildBodyweightTab(), "Bodyweight");
        lo->addWidget(subTabs);
        return w;
    }

    QWidget* buildDashboard() {
        auto *w = new QWidget;
        auto *lo = new QVBoxLayout(w);
        lo->setContentsMargins(8,8,8,8);

        // Heading (center)
        auto *heading = new QLabel("Weekly Activity Summary");
        heading->setAlignment(Qt::AlignCenter);
        heading->setStyleSheet("font-weight:900; font-size:18px; color:#ffffff;");
        lo->addWidget(heading);

        // Helper to create a card and return widgets
        auto makeCard = [&](const QString &title) -> CardWidgets {
            CardWidgets cw;
            cw.card = new QGroupBox;
            auto *cv = new QVBoxLayout(cw.card);
            cw.card->setMaximumHeight(240);
            cw.card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

            auto *t = new QLabel(title);
            t->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
            t->setAlignment(Qt::AlignCenter); // center heading
            cv->addWidget(t);

            cw.bigLbl = new QLabel("--");
            cw.bigLbl->setStyleSheet("font-size:30px; font-weight:900; color:#FFB86B;");
            cw.bigLbl->setAlignment(Qt::AlignCenter); // center main number
            cv->addWidget(cw.bigLbl);

            cw.bar = new QProgressBar;
            cw.bar->setMinimumHeight(18);
            cw.bar->setRange(0,100);
            cw.bar->setTextVisible(true);
            cv->addWidget(cw.bar);

            cw.subLbl = new QLabel("");
            cw.subLbl->setStyleSheet("font-weight:700; color:#ffffff; font-size:11px;");
            cw.subLbl->setAlignment(Qt::AlignCenter); // center sublabel
            cv->addWidget(cw.subLbl);

            return cw;
        };

        // Cardio
        CardWidgets cardioCW = makeCard("Cardio (This week)");
        cardioWeeklyLbl = cardioCW.bigLbl;
        cGoalBar = cardioCW.bar;
        cardioWeeklySub = cardioCW.subLbl;
        lo->addWidget(cardioCW.card);

        // cardio chart
        cardioChart = new WeeklyBarChart;
        cardioChart->setFixedHeight(56);
        static_cast<QVBoxLayout*>(cardioCW.card->layout())->addWidget(cardioChart);

        // Strength
        CardWidgets strengthCW = makeCard("Strength (This week)");
        strengthWeeklyLbl = strengthCW.bigLbl;
        sGoalBar = strengthCW.bar;
        strengthWeeklySub = strengthCW.subLbl;
        lo->addWidget(strengthCW.card);

        strengthChart = new WeeklyBarChart;
        strengthChart->setFixedHeight(56);
        static_cast<QVBoxLayout*>(strengthCW.card->layout())->addWidget(strengthChart);

        // Bodyweight - show daily-weight bars for last 7 days
        CardWidgets bwCW = makeCard("Bodyweight");
        bwCurrentLbl = bwCW.bigLbl;
        if (bwCW.bar) bwCW.bar->hide();        // hide progress bar for bodyweight card
        if (bwCW.subLbl) bwCW.subLbl->clear(); // remove any sub-label text
        lo->addWidget(bwCW.card);

        // bodyweight chart (same style as others)
        bwChart = new WeeklyBarChart;
        bwChart->setFixedHeight(56);
        static_cast<QVBoxLayout*>(bwCW.card->layout())->addWidget(bwChart);

        // small spacer at bottom
        lo->addStretch();

        return w;
    }

    QWidget* buildCardioTab() {
        auto *w = new QWidget;
        auto *lo = new QHBoxLayout(w);

        auto *fg = new QGroupBox;
        auto *fgVBox = new QVBoxLayout;
        auto *fgHeading = new QLabel("Log Cardio");
        fgHeading->setAlignment(Qt::AlignCenter);
        fgHeading->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        fgVBox->addWidget(fgHeading);

        fg->setMaximumWidth(360);
        auto *fl = new QVBoxLayout;
        auto *gr = new QGridLayout;
        gr->setColumnStretch(0, 0);
        gr->setColumnStretch(1, 1);
        gr->setHorizontalSpacing(12);

        QLabel *cardioDateLbl = new QLabel("Date:");
        cardioDateLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        cardioDateLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        gr->addWidget(cardioDateLbl, 0, 0);
        cardioDateEd = new QDateEdit(QDate::currentDate()); cardioDateEd->setCalendarPopup(true);
        cardioDateEd->setMinimumWidth(140); // ensure date visible
        gr->addWidget(cardioDateEd, 0, 1);

        QLabel *cardioTypeLbl = new QLabel("Type:");
        cardioTypeLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        cardioTypeLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        gr->addWidget(cardioTypeLbl, 1, 0);
        cardioTypeCb = new QComboBox; cardioTypeCb->addItems({"Running","Cycling","Swimming","Walking"}); gr->addWidget(cardioTypeCb, 1, 1);

        QLabel *cardioDurLbl = new QLabel("Duration (Min):");
        cardioDurLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        cardioDurLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        gr->addWidget(cardioDurLbl, 2, 0);
        cardioDur = new QSpinBox; cardioDur->setRange(1,500); cardioDur->setValue(30); gr->addWidget(wrapSpinBox(cardioDur), 2, 1);

        QLabel *cardioDistLbl = new QLabel("Distance (Km):");
        cardioDistLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        cardioDistLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        gr->addWidget(cardioDistLbl, 3, 0);
        cardioDist = new QDoubleSpinBox; cardioDist->setRange(0.1,500); cardioDist->setValue(5); gr->addWidget(wrapDoubleSpinBox(cardioDist), 3, 1);

        fl->addLayout(gr);
        auto *sv = new QPushButton("Save Cardio"); connect(sv, &QPushButton::clicked, [this]{ saveCardio(); }); fl->addWidget(sv);
        fl->addStretch();
        fgVBox->addLayout(fl);
        fg->setLayout(fgVBox);

        lo->addWidget(fg);

        auto *hg = new QGroupBox;
        auto *hgVBox = new QVBoxLayout;
        auto *hgHeading = new QLabel("Cardio History");
        hgHeading->setAlignment(Qt::AlignCenter);
        hgHeading->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        hgVBox->addWidget(hgHeading);

        cardioT = new QTableWidget; cardioT->setColumnCount(6);
        cardioT->setHorizontalHeaderLabels({"Date","Type","Duration","Distance","Avg Speed (Km/H)","Calories"});
        cardioT->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        cardioT->setAlternatingRowColors(true);
        hgVBox->addWidget(cardioT);

        auto *db = new QPushButton("Delete"); connect(db, &QPushButton::clicked, [this]{ delCardio(); }); hgVBox->addWidget(db);
        hg->setLayout(hgVBox);

        lo->addWidget(hg);

        return w;
    }

    QWidget* buildStrengthTab() {
        auto *w = new QWidget;
        auto *lo = new QHBoxLayout(w);

        auto *lp = new QWidget; lp->setMaximumWidth(760);
        lp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *ll = new QVBoxLayout(lp);

        auto *dateContainer = new QWidget;
        auto *dateLayout = new QGridLayout(dateContainer);
        dateLayout->setColumnStretch(0, 0);
        dateLayout->setColumnStretch(1, 1);
        dateLayout->setHorizontalSpacing(12);
        auto *dateLabel = new QLabel("Date:");
        dateLabel->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        dateLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        dateLayout->addWidget(dateLabel, 0, 0);
        strDateEd = new QDateEdit(QDate::currentDate()); strDateEd->setCalendarPopup(true);
        strDateEd->setMinimumWidth(180); // ensure date is fully visible
        dateLayout->addWidget(strDateEd, 0, 1);
        ll->addWidget(dateContainer);

        auto *addExHeading = new QLabel("Add Exercise");
        addExHeading->setAlignment(Qt::AlignCenter);
        addExHeading->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        ll->addWidget(addExHeading);

        auto *eg = new QGroupBox; auto *el = new QVBoxLayout(eg);
        auto *r1 = new QHBoxLayout;
        QLabel *exerciseLbl = new QLabel("Exercise:");
        exerciseLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        exerciseLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        r1->addWidget(exerciseLbl);
        exName = new QLineEdit; exName->setPlaceholderText("E.g., Bench Press"); r1->addWidget(exName); el->addLayout(r1);

        auto *r2 = new QHBoxLayout;
        QLabel *setsLbl = new QLabel("Sets:");
        setsLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        r2->addWidget(setsLbl);
        exSets = new QSpinBox; exSets->setRange(1,10); exSets->setValue(3);
        connect(exSets, QOverload<int>::of(&QSpinBox::valueChanged), [this](int n){ updateSetsTable(n); });
        r2->addWidget(wrapSpinBox(exSets));

        QLabel *weightLbl = new QLabel("Weight:");
        weightLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        r2->addWidget(weightLbl);
        exWeight = new QDoubleSpinBox; exWeight->setRange(0,500); exWeight->setValue(20);
        r2->addWidget(wrapDoubleSpinBox(exWeight));

        perSetWeightCb = new QCheckBox("Per-Set Weights");
        perSetWeightCb->setToolTip("If checked, each set can have an independent weight value.");
        connect(perSetWeightCb, &QCheckBox::toggled, [this](bool on){
            if (setsT) {
                setsT->setColumnHidden(2, !on);
            }
        });
        r2->addWidget(perSetWeightCb);
        el->addLayout(r2);

        QLabel *setsDetailLbl = new QLabel("Sets Detail (Reps, Weight):");
        setsDetailLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        el->addWidget(setsDetailLbl);

        setsT = new QTableWidget;
        setsT->setColumnCount(3);
        setsT->setHorizontalHeaderLabels({"Set","Reps","Weight (Kg)"});
        setsT->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        setsT->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
        setsT->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
        setsT->setMinimumWidth(640);
        setsT->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setsT->setMaximumHeight(260);
        el->addWidget(setsT);

        updateSetsTable(3);

        auto *ab = new QPushButton("Add Exercise"); connect(ab, &QPushButton::clicked, [this]{ addExercise(); }); el->addWidget(ab);
        ll->addWidget(eg);

        auto *curWorkoutHeading = new QLabel("Current Workout");
        curWorkoutHeading->setAlignment(Qt::AlignCenter);
        curWorkoutHeading->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        ll->addWidget(curWorkoutHeading);

        auto *cg = new QGroupBox; auto *cgl = new QVBoxLayout(cg); exList = new QListWidget; cgl->addWidget(exList);
        auto *btns = new QHBoxLayout; btns->setAlignment(Qt::AlignCenter);
        auto *clr = new QPushButton("Clear"); connect(clr, &QPushButton::clicked, [this]{ curEx.clear(); exList->clear(); });
        btns->addWidget(clr);
        auto *svw = new QPushButton("Save Workout"); connect(svw, &QPushButton::clicked, [this]{ saveStrength(); });
        btns->addWidget(svw); cgl->addLayout(btns); ll->addWidget(cg);

        lo->addWidget(lp);

        auto *hg = new QGroupBox;
        auto *hgVBox = new QVBoxLayout;
        auto *hgHeading = new QLabel("Strength History");
        hgHeading->setAlignment(Qt::AlignCenter);
        hgHeading->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        hgVBox->addWidget(hgHeading);

        strT = new QTableWidget; strT->setColumnCount(5);
        strT->setHorizontalHeaderLabels({"Date","Exercises","Sets","Reps","Volume"});
        strT->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        connect(strT, &QTableWidget::cellDoubleClicked, [this](int r, int){ showStrDetails(r); });
        hgVBox->addWidget(strT);
        auto *db = new QPushButton("Delete"); connect(db, &QPushButton::clicked, [this]{ delStrength(); }); hgVBox->addWidget(db);
        hg->setLayout(hgVBox);
        lo->addWidget(hg);

        return w;
    }

    QWidget* buildBodyweightTab() {
        auto *w = new QWidget;
        auto *lo = new QVBoxLayout(w);

        auto *heading = new QLabel("Bodyweight Log");
        heading->setAlignment(Qt::AlignCenter);
        heading->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        lo->addWidget(heading);

        auto *form = new QWidget;
        auto *g = new QGridLayout(form);
        g->setColumnStretch(0, 0);
        g->setColumnStretch(1, 1);
        g->setHorizontalSpacing(12);

        QLabel *bwDateLbl = new QLabel("Date:");
        bwDateLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        bwDateLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        g->addWidget(bwDateLbl, 0, 0);
        bwDateEd = new QDateEdit(QDate::currentDate()); bwDateEd->setCalendarPopup(true); g->addWidget(bwDateEd, 0, 1);

        QLabel *bwWeightLbl = new QLabel("Weight (Kg):");
        bwWeightLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        bwWeightLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        g->addWidget(bwWeightLbl, 1, 0);
        bwWeightSp = new QDoubleSpinBox; bwWeightSp->setRange(20,300); bwWeightSp->setDecimals(1); bwWeightSp->setValue(profWeight ? profWeight->value() : 70.0);
        g->addWidget(wrapDoubleSpinBox(bwWeightSp), 1, 1);

        lo->addWidget(form);

        auto *saveBtn = new QPushButton("Save Weight"); connect(saveBtn, &QPushButton::clicked, [this]{ saveBodyweight(); });
        lo->addWidget(saveBtn);

        weightT = new QTableWidget;
        weightT->setColumnCount(2);
        weightT->setHorizontalHeaderLabels({"Date","Weight (Kg)"});
        weightT->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        lo->addWidget(weightT);

        auto *delBtn = new QPushButton("Delete"); connect(delBtn, &QPushButton::clicked, [this]{ delBodyweight(); });
        lo->addWidget(delBtn);

        return w;
    }

    QWidget* buildGoalsTab() {
        auto *w = new QWidget; auto *lo = new QVBoxLayout(w);

        auto *createHeading = new QLabel("Create Goal");
        createHeading->setAlignment(Qt::AlignCenter);
        createHeading->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        lo->addWidget(createHeading);

        auto *ag = new QGroupBox; auto *al = new QVBoxLayout(ag);

        auto *row1 = new QHBoxLayout; auto *nameLbl = new QLabel("Goal Name:"); nameLbl->setStyleSheet("font-weight:900;"); row1->addWidget(nameLbl);
        goalNameEd = new QLineEdit; goalNameEd->setPlaceholderText("E.g., 10K Run Target Or Bench Press"); row1->addWidget(goalNameEd); al->addLayout(row1);

        auto *row2 = new QHBoxLayout; auto *typeLbl = new QLabel("Type:"); typeLbl->setStyleSheet("font-weight:900;"); row2->addWidget(typeLbl);
        goalTypeCb = new QComboBox;
        goalTypeCb->addItems({"Cardio", "Strength"});
        row2->addWidget(goalTypeCb); al->addLayout(row2);

        goalSwitchStack = new QStackedWidget;

        goalCardioPage = new QWidget;
        auto *cGrid = new QGridLayout(goalCardioPage);
        cGrid->setColumnStretch(0, 0);
        cGrid->setColumnStretch(1, 1);
        cGrid->setHorizontalSpacing(12);

        QLabel *goalDistanceLbl = new QLabel("Distance (Km):");
        goalDistanceLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        goalDistanceLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        cGrid->addWidget(goalDistanceLbl, 0, 0);
        goalTargetSp = new QDoubleSpinBox; goalTargetSp->setRange(0,1000); goalTargetSp->setDecimals(0); goalTargetSp->setValue(10);
        cGrid->addWidget(wrapDoubleSpinBox(goalTargetSp), 0, 1);

        QLabel *goalTimeLbl = new QLabel("Target Time (Min):");
        goalTimeLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        goalTimeLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        cGrid->addWidget(goalTimeLbl, 1, 0);
        goalTargetTimeSp = new QSpinBox; goalTargetTimeSp->setRange(0,1000); goalTargetTimeSp->setValue(0); goalTargetTimeSp->setSpecialValueText("No Time Limit");
        cGrid->addWidget(wrapSpinBox(goalTargetTimeSp), 1, 1);

        goalSwitchStack->addWidget(goalCardioPage);

        goalStrengthPage = new QWidget;
        auto *sGrid = new QGridLayout(goalStrengthPage);

        QLabel *goalExNameLbl = new QLabel("Exercise Name:");
        goalExNameLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        sGrid->addWidget(goalExNameLbl, 0, 0);
        goalExNameEd = new QLineEdit; goalExNameEd->setPlaceholderText("E.g., Bench Press"); sGrid->addWidget(goalExNameEd, 0, 1);

        QLabel *goalExWeightLbl = new QLabel("Weight (Kg):");
        goalExWeightLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        sGrid->addWidget(goalExWeightLbl, 1, 0);
        goalExWeightSp = new QDoubleSpinBox; goalExWeightSp->setRange(0,500); goalExWeightSp->setValue(20); sGrid->addWidget(wrapDoubleSpinBox(goalExWeightSp), 1, 1);

        QLabel *goalExSetsLbl = new QLabel("Sets:");
        goalExSetsLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        sGrid->addWidget(goalExSetsLbl, 2, 0);
        goalExSetsSp = new QSpinBox; goalExSetsSp->setRange(1,50); goalExSetsSp->setValue(3); sGrid->addWidget(wrapSpinBox(goalExSetsSp), 2, 1);

        QLabel *goalExRepsLbl = new QLabel("Reps:");
        goalExRepsLbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
        sGrid->addWidget(goalExRepsLbl, 3, 0);
        goalExRepsSp = new QSpinBox; goalExRepsSp->setRange(1,200); goalExRepsSp->setValue(12); sGrid->addWidget(wrapSpinBox(goalExRepsSp), 3, 1);

        goalSwitchStack->addWidget(goalStrengthPage);

        al->addWidget(goalSwitchStack);

        connect(goalTypeCb, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int idx){
            if (goalSwitchStack) goalSwitchStack->setCurrentIndex(idx);
        });

        goalSwitchStack->setCurrentIndex(0);

        auto *ab = new QPushButton("Add Goal"); connect(ab, &QPushButton::clicked, [this]{ addGoal(); }); al->addWidget(ab);
        lo->addWidget(ag);

        auto *myGoalsHeading = new QLabel("My Goals");
        myGoalsHeading->setAlignment(Qt::AlignCenter);
        myGoalsHeading->setStyleSheet("font-weight:900; font-size:16px; color:#ffffff;");
        lo->addWidget(myGoalsHeading);

        auto *lg = new QGroupBox; auto *ll = new QVBoxLayout(lg); goalsT = new QTableWidget; goalsT->setColumnCount(6);
        goalsT->setHorizontalHeaderLabels({"Name","Type","Distance (Km)","Progress","Time Target","Status"}); goalsT->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        goalsT->setAlternatingRowColors(true);
        ll->addWidget(goalsT);

        auto *db = new QPushButton("Delete");
        connect(db, &QPushButton::clicked, [this]{ delGoal(); });
        ll->addWidget(db);
        lo->addWidget(lg);

        return w;
    }

    QWidget* buildProfileTab() {
        auto *w = new QWidget; auto *lo = new QVBoxLayout(w);

        // Profile heading centered & bold
        auto *profileHeading = new QLabel("Profile");
        profileHeading->setAlignment(Qt::AlignCenter);
        profileHeading->setStyleSheet("font-weight:900; font-size:18px; color:#ffffff;");
        lo->addWidget(profileHeading);

        auto *pg = new QGroupBox; auto *g = new QGridLayout(pg);
        // Use LEFT-aligned & bold labels for profile fields
        auto addBoldLeftLabel = [&](const QString &text, int row, int col) {
            QLabel *lbl = new QLabel(text);
            lbl->setStyleSheet("font-weight:900; color:#ffffff; font-size:13px;");
            lbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            g->addWidget(lbl, row, col);
        };

        addBoldLeftLabel("Gender:", 0, 0); editGender = new QComboBox; editGender->addItems({"Male","Female","Other"}); g->addWidget(editGender, 0, 1);
        addBoldLeftLabel("Weight (Kg):", 1, 0); editWeight = new QDoubleSpinBox; editWeight->setRange(20,300); g->addWidget(wrapDoubleSpinBox(editWeight), 1, 1);
        addBoldLeftLabel("Target Bodyweight (Kg):", 2, 0); editTargetBodyweight = new QDoubleSpinBox; editTargetBodyweight->setRange(20,300); editTargetBodyweight->setDecimals(1); g->addWidget(wrapDoubleSpinBox(editTargetBodyweight), 2, 1);
        addBoldLeftLabel("Height (Cm):", 3, 0); editHeight = new QDoubleSpinBox; editHeight->setRange(100,250); g->addWidget(wrapDoubleSpinBox(editHeight), 3, 1);
        addBoldLeftLabel("Age:", 4, 0); editAge = new QSpinBox; editAge->setRange(10,120); g->addWidget(wrapSpinBox(editAge), 4, 1);
        auto *ub = new QPushButton("Update"); connect(ub, &QPushButton::clicked, [this]{ updateProfile(); }); g->addWidget(ub, 5, 0, 1, 2);
        lo->addWidget(pg);

        // BMI group centered & bold
        auto *bg = new QGroupBox; auto *bl = new QVBoxLayout(bg);
        bmiLbl = new QLabel("--"); bmiLbl->setStyleSheet("font-size:24px;font-weight:900;color:#FFB86B"); bmiLbl->setAlignment(Qt::AlignCenter); bl->addWidget(bmiLbl);
        bmiCat = new QLabel("Category: --"); bmiCat->setAlignment(Qt::AlignCenter); bmiCat->setStyleSheet("font-weight:900; color:#ffffff;"); bl->addWidget(bmiCat);
        lo->addWidget(bg);

        // Place logout at bottom-left
        lo->addStretch();
        auto *logoutBtn = new QPushButton("Logout");
        connect(logoutBtn, &QPushButton::clicked, [this]{ doLogout(); });
        lo->addWidget(logoutBtn, 0, Qt::AlignLeft);

        return w;
    }

    // Helpers: file/user storage
    QString hash(const QString &s) { return QString(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex()); }

    bool userExists(const QString &u) {
        QFile f("users.dat");
        if (!f.open(QIODevice::ReadOnly)) return false;
        QTextStream in(&f);
        while (!in.atEnd()) if (in.readLine().split("|").value(0) == u) { f.close(); return true; }
        f.close();
        return false;
    }

    bool checkLogin(const QString &u, const QString &p) {
        QFile f("users.dat");
        if (!f.open(QIODevice::ReadOnly)) return false;
        QTextStream in(&f);
        QString h = hash(p);
        while (!in.atEnd()) {
            auto parts = in.readLine().split("|");
            if (parts.size() >= 3 && parts[0] == u && parts[1] == h) { pName = parts[2]; f.close(); return true; }
        }
        f.close();
        return false;
    }

    void saveUser(const QString &u, const QString &p, const QString &n) {
        QFile f("users.dat");
        f.open(QIODevice::Append);
        QTextStream(&f) << u << "|" << hash(p) << "|" << n << "\n";
        f.close();
    }

    void loadData() {
        cardio.clear(); strength.clear(); weightLogs.clear(); goals.clear();
        QString u = user.username;

        QFile pf("profile_" + u + ".dat");
        if (pf.open(QIODevice::ReadOnly)) {
            auto p = QTextStream(&pf).readLine().split("|");
            // profile format: gender|weight|targetBodyweight|height|age
            if (p.size() >= 5) { user.gender = p[0]; user.weight = p[1].toDouble(); user.targetBodyweight = p[2].toDouble(); user.height = p[3].toDouble(); user.age = p[4].toInt(); }
            pf.close();
        }

        QFile cf("cardio_" + u + ".dat");
        if (cf.open(QIODevice::ReadOnly)) {
            QTextStream in(&cf);
            while (!in.atEnd()) {
                auto p = in.readLine().split("|");
                // Format: date|type|duration|distance|calories|avgSpeed
                if (p.size() >= 6) {
                    CardioWorkout w;
                    w.date = p[0];
                    w.type = p[1];
                    w.duration = p[2].toInt();
                    w.distance = p[3].toDouble();
                    w.calories = p[4].toDouble();
                    w.avgSpeed = p[5].toDouble();
                    cardio.push_back(w);
                }
            }
            cf.close();
        }

        QFile sf("strength_" + u + ".dat");
        if (sf.open(QIODevice::ReadOnly)) {
            QTextStream in(&sf);
            while (!in.atEnd()) {
                auto p = in.readLine().split("|");
                // Format: date|calories|exercises
                if (p.size() >= 3) {
                    StrengthWorkout w; w.date = p[0]; if (p.size() >= 2) w.calories = p[1].toDouble();
                    if (p.size() >= 3) {
                        for (const QString &es : p[2].split(";")) {
                            auto ep = es.split(":");
                            if (ep.size() >= 2) {
                                Exercise ex; ex.name = ep[0];
                                for (const QString &ss : ep[1].split(",")) {
                                    auto sp = ss.split("x");
                                    if (sp.size() >= 2) { ExerciseSet s; s.reps = sp[0].toInt(); s.weight = sp[1].toDouble(); ex.sets.push_back(s); }
                                }
                                w.exercises.push_back(ex);
                            }
                        }
                    }
                    strength.push_back(w);
                }
            }
            sf.close();
        }

        QFile wf("weight_" + u + ".dat");
        if (wf.open(QIODevice::ReadOnly)) {
            QTextStream in(&wf);
            while (!in.atEnd()) {
                auto p = in.readLine().split("|");
                // Format: date|weight
                if (p.size() >= 2) {
                    BodyweightLog b; b.date = p[0]; b.weight = p[1].toDouble();
                    weightLogs.push_back(b);
                }
            }
            wf.close();
        }

        QFile gf("goals_" + u + ".dat");
        if (gf.open(QIODevice::ReadOnly)) {
            QTextStream in(&gf);
            while (!in.atEnd()) {
                auto p = in.readLine().split("|");
                if (p.size() >= 10) {
                    Goal g;
                    g.name = p[0]; g.type = p[1]; g.target = p[2].toDouble(); g.progress = p[3].toDouble();
                    g.targetTime = p[4].toInt(); g.progressTime = p[5].toInt();
                    g.exerciseName = p[6]; g.exWeight = p[7].toDouble(); g.exSets = p[8].toInt(); g.exReps = p[9].toInt();
                    goals.push_back(g);
                } else if (p.size() >= 6) {
                    Goal g; g.name = p[0]; g.type = p[1]; g.target = p[2].toDouble(); g.progress = p[3].toDouble();
                    g.targetTime = p[4].toInt(); g.progressTime = p[5].toInt();
                    goals.push_back(g);
                }
            }
            gf.close();
        }
    }

    void saveData() {
        QString u = user.username;
        QFile pf("profile_" + u + ".dat"); pf.open(QIODevice::WriteOnly);
        QTextStream(&pf) << user.gender << "|" << user.weight << "|" << user.targetBodyweight << "|" << user.height << "|" << user.age << "\n";
        pf.close();

        QFile cf("cardio_" + u + ".dat"); cf.open(QIODevice::WriteOnly);
        QTextStream co(&cf);
        for (auto &w : cardio) co << w.date << "|" << w.type << "|" << w.duration << "|" << w.distance << "|" << w.calories << "|" << w.avgSpeed << "\n";
        cf.close();

        QFile sf("strength_" + u + ".dat"); sf.open(QIODevice::WriteOnly);
        QTextStream so(&sf);
        for (auto &w : strength) {
            so << w.date << "|" << w.calories << "|";
            for (size_t i = 0; i < w.exercises.size(); i++) {
                auto &ex = w.exercises[i];
                so << ex.name << ":";
                for (size_t j = 0; j < ex.sets.size(); j++) { so << ex.sets[j].reps << "x" << ex.sets[j].weight; if (j < ex.sets.size()-1) so << ","; }
                if (i < w.exercises.size()-1) so << ";";
            }
            so << "\n";
        }
        sf.close();

        QFile wf("weight_" + u + ".dat"); wf.open(QIODevice::WriteOnly);
        QTextStream wout(&wf);
        for (auto &b : weightLogs) wout << b.date << "|" << b.weight << "\n";
        wf.close();

        QFile gf("goals_" + u + ".dat"); gf.open(QIODevice::WriteOnly);
        QTextStream go(&gf);
        for (auto &g : goals) {
            go << g.name << "|" << g.type << "|" << g.target << "|" << g.progress << "|" << g.targetTime << "|" << g.progressTime << "|"
               << g.exerciseName << "|" << g.exWeight << "|" << g.exSets << "|" << g.exReps << "\n";
        }
        gf.close();
    }

    // Utility calculators
    double calcCardioCal(const QString &t, int d) {
        double met = t == "Running" ? 9.8 : (t == "Swimming" ? 8.0 : (t == "Walking" ? 3.5 : 7.5));
        return met * (user.weight > 0 ? user.weight : 70) * (d / 60.0);
    }

    double calcStrCal(double v) { return 5.0 * (user.weight > 0 ? user.weight : 70) * 0.15 + v * 0.01; }

    // Refresh UI (tables, dashboard, profile)
    void refresh() {
        // Aggregate weekly values and per-day arrays for charts
        QDate today = QDate::currentDate();
        QDate wkAgo = today.addDays(-6); // include today, last 7 days (6 days ago..today)

        QVector<double> cardioPerDay(7, 0.0);     // index 0 = 6 days ago, 6 = today
        QVector<double> strengthPerDay(7, 0.0);
        QVector<double> weightPerDay(7, 0.0);

        double cardioKmWeek = 0.0;
        double cardioCaloriesWeek = 0.0;
        for (auto &w : cardio) {
            QDate d = QDate::fromString(w.date, "yyyy-MM-dd");
            if (!d.isValid()) continue;
            int days = d.daysTo(today); // days from that day to today (0 = today)
            if (days >= 0 && days < 7) {
                int idx = 6 - days;
                cardioPerDay[idx] += w.distance;
            }
            if (d >= wkAgo && d <= today) {
                cardioKmWeek += w.distance;
                cardioCaloriesWeek += w.calories;
            }
        }

        int strengthWorkoutsWeek = 0;
        double strengthVolumeWeek = 0.0;
        double strengthCaloriesWeek = 0.0;
        for (auto &w : strength) {
            QDate d = QDate::fromString(w.date, "yyyy-MM-dd");
            if (!d.isValid()) continue;
            int days = d.daysTo(today);
            if (days >= 0 && days < 7) {
                int idx = 6 - days;
                double vol = 0;
                for (auto &e : w.exercises) for (auto &s : e.sets) vol += s.reps * s.weight;
                strengthPerDay[idx] += vol; // per-day volume (kg)
            }
            if (d >= wkAgo && d <= today) {
                strengthWorkoutsWeek++;
                for (auto &e : w.exercises) for (auto &s : e.sets) strengthVolumeWeek += s.reps * s.weight;
                strengthCaloriesWeek += w.calories;
            }
        }

        // Bodyweight: build a map date->weight for quick lookup, then fill last 7 days
        QMap<QDate,double> weightByDate;
        for (auto &b : weightLogs) {
            QDate d = QDate::fromString(b.date, "yyyy-MM-dd");
            if (!d.isValid()) continue;
            weightByDate[d] = b.weight; // overwrite ensures latest for that date
        }
        for (int i = 0; i < 7; ++i) {
            QDate d = today.addDays(i - 6); // i=0 => 6 days ago, i=6 => today
            if (weightByDate.contains(d)) weightPerDay[i] = weightByDate[d];
            else weightPerDay[i] = 0.0;
        }

        // Update cardio widgets
        if (cardioWeeklyLbl) {
            double sumKm = 0.0;
            for (double v : cardioPerDay) sumKm += v;
            cardioWeeklyLbl->setText(QString("%1 km").arg(sumKm, 0, 'f', 1));
        }
        double cardioTarget = 20.0;
        for (auto &g : goals) if (g.type == "cardio_km") cardioTarget = g.target > 0 ? g.target : cardioTarget;
        double cardioSum = 0;
        for (double v : cardioPerDay) cardioSum += v;
        int cardioPct = cardioTarget > 0 ? qBound(0, (int)qRound(cardioSum * 100.0 / cardioTarget), 100) : 0;
        if (cGoalBar) { cGoalBar->setValue(cardioPct); cGoalBar->setFormat(QString("%1%").arg(cardioPct)); }
        if (cardioWeeklySub) cardioWeeklySub->setText(QString("%1 km in last 7d • target %2 km").arg(QString::number(cardioSum, 'f', 1)).arg(cardioTarget,0,'f',1));

        // Update strength widgets
        if (strengthWeeklyLbl) strengthWeeklyLbl->setText(QString("%1").arg(strengthWorkoutsWeek));
        int strengthTarget = 3;
        for (auto &g : goals) if (g.type == "strength_exercise") { strengthTarget = qMax(1, g.target > 0 ? (int)g.target : strengthTarget); }
        int strengthPct = strengthTarget > 0 ? qBound(0, (int)qRound((strengthWorkoutsWeek * 100.0) / (double)strengthTarget), 100) : 0;
        if (sGoalBar) { sGoalBar->setValue(strengthPct); sGoalBar->setFormat(QString("%1%").arg(strengthPct)); }
        if (strengthWeeklySub) strengthWeeklySub->setText(QString("%1/%2 workouts • %3 kg vol")
                                           .arg(strengthWorkoutsWeek)
                                           .arg(strengthTarget)
                                           .arg((int)strengthVolumeWeek));

        // Update bodyweight widgets
        double currentWeight = user.weight > 0 ? user.weight : (weightLogs.empty() ? 0.0 : weightLogs.back().weight);
        if (bwCurrentLbl) {
            if (currentWeight > 0) bwCurrentLbl->setText(QString("%1 kg").arg(currentWeight,0,'f',1));
            else bwCurrentLbl->setText("-- kg");
        }

        // update charts
        if (cardioChart) cardioChart->setData(cardioPerDay);
        if (strengthChart) strengthChart->setData(strengthPerDay);
        if (bwChart) bwChart->setData(weightPerDay); // <-- bodyweight chart shows daily weights over last 7 days

        // populate small stats and tables
        double cd = 0, cc = 0;
        for (auto &w : cardio) { cd += w.distance; cc += w.calories; }
        if (!cCnt) cCnt = new QLabel; if (!cDist) cDist = new QLabel; if (!cCal) cCal = new QLabel;
        cCnt->setText(QString::number(cardio.size()));
        cDist->setText(QString::number(cd, 'f', 1));
        cCal->setText(QString::number((int)cc));

        double sv = 0, sc = 0;
        for (auto &w : strength) { sc += w.calories; for (auto &e : w.exercises) for (auto &s : e.sets) sv += s.reps * s.weight; }
        if (!sCnt) sCnt = new QLabel; if (!sVol) sVol = new QLabel; if (!sCal) sCal = new QLabel;
        sCnt->setText(QString::number(strength.size()));
        sVol->setText(QString::number((int)sv));
        sCal->setText(QString::number((int)sc));

        cardioT->setRowCount((int)cardio.size());
        for (size_t i = 0; i < cardio.size(); i++) {
            auto &w = cardio[i];
            cardioT->setItem((int)i, 0, new QTableWidgetItem(w.date));
            cardioT->setItem((int)i, 1, new QTableWidgetItem(w.type));
            cardioT->setItem((int)i, 2, new QTableWidgetItem(QString::number(w.duration) + " min"));
            cardioT->setItem((int)i, 3, new QTableWidgetItem(QString::number(w.distance, 'f', 2) + " km"));
            cardioT->setItem((int)i, 4, new QTableWidgetItem(QString::number(w.avgSpeed, 'f', 2) + " km/h"));
            cardioT->setItem((int)i, 5, new QTableWidgetItem(QString::number((int)w.calories) + " cal"));
        }

        strT->setRowCount((int)strength.size());
        for (size_t i = 0; i < strength.size(); i++) {
            auto &w = strength[i];
            int ts = 0, tr = 0; double tv = 0;
            for (auto &e : w.exercises) { ts += (int)e.sets.size(); for (auto &s : e.sets) { tr += s.reps; tv += s.reps * s.weight; } }
            strT->setItem((int)i, 0, new QTableWidgetItem(w.date));
            strT->setItem((int)i, 1, new QTableWidgetItem(QString::number(w.exercises.size())));
            strT->setItem((int)i, 2, new QTableWidgetItem(QString::number(ts)));
            strT->setItem((int)i, 3, new QTableWidgetItem(QString::number(tr)));
            strT->setItem((int)i, 4, new QTableWidgetItem(QString::number((int)tv) + " kg"));
        }

        goalsT->setRowCount((int)goals.size());
        for (size_t i = 0; i < goals.size(); i++) {
            auto &g = goals[i];
            goalsT->setItem((int)i, 0, new QTableWidgetItem(g.name));
            QString typeStr = (g.type == "cardio_km") ? "Cardio" : (g.type == "strength_exercise" ? "Strength" : g.type);
            goalsT->setItem((int)i, 1, new QTableWidgetItem(typeStr));

            if (g.type == "cardio_km") {
                goalsT->setItem((int)i, 2, new QTableWidgetItem(QString::number(g.target)));
            } else {
                goalsT->setItem((int)i, 2, new QTableWidgetItem("--"));
            }

            goalsT->setItem((int)i, 3, new QTableWidgetItem(QString::number(g.progress, 'f', 1)));

            if (g.type == "cardio_km" && g.targetTime > 0) {
                goalsT->setItem((int)i, 4, new QTableWidgetItem(QString("%1/%2 min").arg(g.progressTime).arg(g.targetTime)));
            } else {
                goalsT->setItem((int)i, 4, new QTableWidgetItem("--"));
            }

            int pct = g.target > 0 ? qMin(100, (int)(g.progress * 100 / g.target)) : 0;
            auto *bar = new QProgressBar; bar->setValue(pct);
            bar->setFormat(pct >= 100 ? "Done" : QString("%1%").arg(pct));
            goalsT->setCellWidget((int)i, 5, bar);
        }

        weightT->setRowCount((int)weightLogs.size());
        for (size_t i = 0; i < weightLogs.size(); ++i) {
            weightT->setItem((int)i, 0, new QTableWidgetItem(weightLogs[i].date));
            weightT->setItem((int)i, 1, new QTableWidgetItem(QString::number(weightLogs[i].weight, 'f', 1) + " kg"));
        }

        // update profile fields & BMI
        editGender->setCurrentIndex(user.gender == "Female" ? 1 : (user.gender == "Other" ? 2 : 0));
        editWeight->setValue(user.weight > 0 ? user.weight : 70);
        editTargetBodyweight->setValue(user.targetBodyweight > 0 ? user.targetBodyweight : (user.weight>0?user.weight:70));
        editHeight->setValue(user.height > 0 ? user.height : 170);
        editAge->setValue(user.age > 0 ? user.age : 25);
        double bmi = user.height > 0 ? user.weight / ((user.height/100)*(user.height/100)) : 0;
        bmiLbl->setText(bmi > 0 ? QString::number(bmi, 'f', 1) : "--");
        QString cat = "N/A"; if (bmi > 0) { if (bmi < 18.5) cat = "Underweight"; else if (bmi < 25) cat = "Normal"; else if (bmi < 30) cat = "Overweight"; else cat = "Obese"; }
        bmiCat->setText("Category: " + cat);

        // update welcome
        userLbl->setText(user.name);
        welLblMain->setText("Welcome");
    }

    void updateSetsTable(int n) {
        if (!setsT) return;
        setsT->setRowCount(n);
        for (int i = 0; i < n; i++) {
            setsT->setItem(i, 0, new QTableWidgetItem(QString("Set %1").arg(i+1)));

            auto *sp = new QSpinBox;
            sp->setRange(1, 100);
            sp->setValue(10);
            sp->setButtonSymbols(QAbstractSpinBox::PlusMinus);

            if (auto *le = sp->findChild<QLineEdit*>()) {
                le->setStyleSheet("color: #ffffff; background: #071b2a; padding-left:6px;");
            }
            sp->setMinimumWidth(180);
            sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            setsT->setCellWidget(i, 1, sp);

            auto *wsp = new QDoubleSpinBox;
            wsp->setRange(0, 500);
            wsp->setDecimals(1);
            wsp->setValue(exWeight ? exWeight->value() : 20.0);
            wsp->setButtonSymbols(QAbstractSpinBox::PlusMinus);
            if (auto *le2 = wsp->findChild<QLineEdit*>()) {
                le2->setStyleSheet("color: #ffffff; background: #071b2a; padding-left:6px;");
            }
            wsp->setMinimumWidth(150);
            wsp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            setsT->setCellWidget(i, 2, wsp);
        }
        if (perSetWeightCb) {
            setsT->setColumnHidden(2, !perSetWeightCb->isChecked());
        }

        setsT->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        setsT->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        setsT->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    }

    // Actions
    void doLogin() {
        QString u = logUser->text().trimmed(), p = logPass->text();
        if (u.isEmpty() || p.isEmpty()) { QMessageBox::warning(this, "Error", "Enter username and password"); return; }
        if (checkLogin(u, p)) {
            user.username = u; user.name = pName; loadData(); // set user and load
            userLbl->setText(pName);
            welLblMain->setText("Welcome");
            logUser->clear(); logPass->clear(); stack->setCurrentWidget(mainPage);
            refresh();
        } else { QMessageBox::warning(this, "Error", "Invalid credentials"); }
    }

    void doSignup() {
        QString n = sigName->text().trimmed(), u = sigUser->text().trimmed(), p = sigPass->text(), c = sigConf->text();
        if (n.isEmpty() || u.isEmpty() || p.isEmpty()) { QMessageBox::warning(this, "Error", "Fill all fields"); return; }
        if (p.length() < 6) { QMessageBox::warning(this, "Error", "Password min 6 chars"); return; }
        if (p != c) { QMessageBox::warning(this, "Error", "Passwords don't match"); return; }
        if (userExists(u)) { QMessageBox::warning(this, "Error", "Username taken"); return; }
        pUser = u; pName = n; saveUser(u, p, n);
        sigName->clear(); sigUser->clear(); sigPass->clear(); sigConf->clear();
        stack->setCurrentWidget(profilePage);
    }

    void doCompleteProfile() {
        user.username = pUser; user.name = pName;
        user.gender = profGender->currentText(); user.weight = profWeight->value(); user.targetBodyweight = targetBodyweightSp->value(); user.height = profHeight->value(); user.age = profAge->value();
        saveData();
        userLbl->setText(pName);
        welLblMain->setText("Welcome");
        refresh();
        QMessageBox::information(this, "Success", "Profile created!"); stack->setCurrentWidget(mainPage);
    }

    void doLogout() {
        saveData(); user = UserProfile(); cardio.clear(); strength.clear(); weightLogs.clear(); goals.clear(); curEx.clear(); if (exList) exList->clear();
        userLbl->setText("");
        welLblMain->setText("Welcome");
        stack->setCurrentWidget(loginPage);
    }

    void saveCardio() {
        CardioWorkout w; w.date = cardioDateEd->date().toString("yyyy-MM-dd"); w.type = cardioTypeCb->currentText(); w.duration = cardioDur->value(); w.distance = cardioDist->value();
        w.calories = calcCardioCal(w.type, w.duration);
        w.avgSpeed = (w.duration > 0) ? (w.distance * 60.0 / w.duration) : 0.0;
        cardio.push_back(w);

        for (auto &g : goals) {
            if (g.type == "cardio_km") {
                g.progress += w.distance;
                if (g.targetTime > 0) g.progressTime += w.duration;
            }
        }

        saveData(); refresh(); QMessageBox::information(this, "Success", "Cardio saved!");
    }

    void delCardio() {
        int r = cardioT->currentRow();
        if (r >= 0 && r < (int)cardio.size()) { cardio.erase(cardio.begin() + r); saveData(); refresh(); }
    }

    void addExercise() {
        QString n = exName->text().trimmed();
        if (n.isEmpty()) { QMessageBox::warning(this, "Error", "Enter exercise name"); return; }
        Exercise ex; ex.name = n;

        for (int i = 0; i < setsT->rowCount(); ++i) {
            auto *repWidget = qobject_cast<QSpinBox*>(setsT->cellWidget(i, 1));
            int reps = repWidget ? repWidget->value() : 0;
            double weightForSet = exWeight->value();
            if (perSetWeightCb && perSetWeightCb->isChecked()) {
                auto *wWidget = qobject_cast<QDoubleSpinBox*>(setsT->cellWidget(i, 2));
                if (wWidget) weightForSet = wWidget->value();
            }
            ExerciseSet s; s.reps = reps; s.weight = weightForSet;
            ex.sets.push_back(s);
        }

        curEx.push_back(ex);
        QString disp = n + " - ";
        for (size_t i = 0; i < ex.sets.size(); i++) { disp += QString::number(ex.sets[i].reps); if (i < ex.sets.size()-1) disp += ","; }
        disp += QString(" reps @ %1kg").arg(ex.sets.empty() ? exWeight->value() : ex.sets[0].weight);
        exList->addItem(disp);
        exName->clear();
        QMessageBox::information(this, "Added", "Exercise added!");
    }

    void saveStrength() {
        if (curEx.empty()) { QMessageBox::warning(this, "Error", "Add at least one exercise"); return; }
        StrengthWorkout w; w.date = strDateEd->date().toString("yyyy-MM-dd"); w.exercises = curEx;
        double v = 0; for (auto &e : w.exercises) for (auto &s : e.sets) v += s.reps * s.weight; w.calories = calcStrCal(v);
        strength.push_back(w);

        for (auto &g : goals) {
            if (g.type == "strength_exercise" && !g.exerciseName.isEmpty()) {
                bool achievedThisWorkout = false;
                for (auto &e : w.exercises) {
                    if (e.name.compare(g.exerciseName, Qt::CaseInsensitive) == 0) {
                        if ((int)e.sets.size() >= g.exSets) {
                            for (auto &s : e.sets) {
                                if (s.reps >= g.exReps && s.weight >= g.exWeight) { achievedThisWorkout = true; break; }
                            }
                        }
                    }
                    if (achievedThisWorkout) break;
                }
                if (achievedThisWorkout) g.progress += 1;
            }
        }

        curEx.clear(); exList->clear(); saveData(); refresh(); QMessageBox::information(this, "Success", "Strength workout saved!");
    }

    void delStrength() {
        int r = strT->currentRow();
        if (r >= 0 && r < (int)strength.size()) { strength.erase(strength.begin() + r); saveData(); refresh(); }
    }

    void showStrDetails(int r) {
        if (r < 0 || r >= (int)strength.size()) return;
        auto &w = strength[r];
        QString msg = "Workout: " + w.date + "\n\n"; double tv = 0;
        for (auto &e : w.exercises) {
            msg += e.name + "\n"; double ev = 0;
            for (size_t j = 0; j < e.sets.size(); j++) {
                msg += QString("   Set %1: %2 reps @ %3 kg\n").arg(j+1).arg(e.sets[j].reps).arg(e.sets[j].weight);
                ev += e.sets[j].reps * e.sets[j].weight;
            }
            msg += QString("   Volume: %1 kg\n\n").arg((int)ev); tv += ev;
        }
        msg += QString("Total Volume: %1 kg\nCalories: %2").arg((int)tv).arg((int)w.calories);
        QMessageBox::information(this, "Workout Details", msg);
    }

    void addGoal() {
        QString n = goalNameEd->text().trimmed(); if (n.isEmpty()) { QMessageBox::warning(this, "Error", "Enter goal name"); return; }
        Goal g; g.name = n; g.progress = 0; g.targetTime = 0; g.progressTime = 0;
        int idx = goalTypeCb->currentIndex();
        if (idx == 0) {
            g.type = "cardio_km";
            g.target = goalTargetSp->value();
            g.targetTime = goalTargetTimeSp->value();
        } else {
            g.type = "strength_exercise";
            g.exerciseName = goalExNameEd->text().trimmed();
            if (g.exerciseName.isEmpty()) { QMessageBox::warning(this, "Error", "Enter exercise name"); return; }
            g.exWeight = goalExWeightSp->value();
            g.exSets = goalExSetsSp->value(); g.exReps = goalExRepsSp->value();
            g.target = 1;
        }

        goals.push_back(g); saveData(); refresh();

        goalNameEd->clear();
        goalTargetTimeSp->setValue(0);
        goalExNameEd->clear();
        goalExWeightSp->setValue(20);
        goalExSetsSp->setValue(3);
        goalExRepsSp->setValue(12);
        goalTargetSp->setValue(10);

        QMessageBox::information(this, "Success", "Goal created!");
    }

    void delGoal() {
        int r = goalsT->currentRow();
        if (r >= 0 && r < (int)goals.size()) { goals.erase(goals.begin() + r); saveData(); refresh(); }
    }

    void saveBodyweight() {
        BodyweightLog b; b.date = bwDateEd->date().toString("yyyy-MM-dd"); b.weight = bwWeightSp->value();
        weightLogs.push_back(b);

        // Keep user's profile weight synced with last logged bodyweight
        user.weight = b.weight;

        saveData(); refresh(); QMessageBox::information(this, "Success", "Weight logged!");
    }

    void delBodyweight() {
        int r = weightT->currentRow();
        if (r >= 0 && r < (int)weightLogs.size()) { weightLogs.erase(weightLogs.begin() + r); saveData(); refresh(); }
    }

    void updateProfile() {
        user.gender = editGender->currentText(); user.weight = editWeight->value(); user.targetBodyweight = editTargetBodyweight->value(); user.height = editHeight->value(); user.age = editAge->value();
        saveData(); refresh(); QMessageBox::information(this, "Success", "Profile updated!");
    }
};

int main(int argc, char *argv[]) {


    QApplication a(argc, argv);
    FitTrackPro w;
    w.show();
    return a.exec();
}

// at end of main.cpp (after all class definitions)
#include "main.moc"

